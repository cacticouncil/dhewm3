/*
===========================================================================
File: BotManager.h
Author: John Wileczek
Description: Bot client management involving adding, removing, and event
dispatch to bots.
===========================================================================
*/
#include "precompiled.h"


#ifdef AFI_BOTS

#include "BotManager.h"
#include "BotBrain.h"


afiBotManager				afiBotManagerLocal;
afiBotManager *				BotManager = &afiBotManagerLocal;


int							afiBotManager::numQueBots = 0;
int							afiBotManager::botEntityDefNumber[MAX_CLIENTS];
bool						afiBotManager::botSpawned[MAX_CLIENTS];
idCmdArgs					afiBotManager::cmdQue[MAX_CLIENTS];
idCmdArgs					afiBotManager::persistArgs[MAX_CLIENTS];
usercmd_t					afiBotManager::botCmds[MAX_CLIENTS];
idList<botInfo_t*>			afiBotManager::loadedBots;
afiBotBrain*				afiBotManager::brainFastList[MAX_CLIENTS];

#ifdef AFI_BOTS

BOOST_PYTHON_MODULE(afiBotManager) {

	enum_<flagStatus_t>("flagStatus_t")
		.value("FLAGSTATUS_INBASE",FLAGSTATUS_INBASE)
		.value("FLAGSTATUS_TAKEN",FLAGSTATUS_TAKEN)
		.value("FLAGSTATUS_STRAY",FLAGSTATUS_STRAY)
		;

	class_<afiBotManager>("afiBotManager")
	.def("GetFlag",&afiBotManager::GetFlag,return_internal_reference<>())
	.def("GetFlagStatus",&afiBotManager::GetFlagStatus)
	.def("ConsolePrint",&afiBotManager::ConsolePrint)
	.staticmethod("GetFlag")
	.staticmethod("GetFlagStatus")
	.staticmethod("ConsolePrint")
	;
}
#endif

 void afiBotManager::PrintInfo( void ) {
	common->Printf("AFI Bots Initialized\n");
}

 void afiBotManager::ConsolePrint(const char* string) {
	 gameLocal.Printf(string);
 }

 void afiBotManager::Initialize( void ) {
	for ( int i = 0; i < MAX_CLIENTS; i++ ) {
		cmdQue[i].Clear();
		persistArgs[i].Clear();
		botSpawned[i] = false;
		botEntityDefNumber[i] = 0;
		brainFastList[i] = NULL;
	}
	loadedBots.Clear();
	memset( &botCmds, 0, sizeof( botCmds ) );
	
}

 void afiBotManager::Shutdown( void ) {
	for ( int i = 0; i < MAX_CLIENTS; i++ ) {
		cmdQue[i].Clear();
		persistArgs[i].Clear();
		botSpawned[i] = false;
		botEntityDefNumber[i] = 0;
		brainFastList[i] = NULL;
	}
	
	
	//delete the memory for both the list, python is handling the cleanup of
	// bot information.
	loadedBots.Clear();
	memset( &botCmds, 0, sizeof( botCmds ) );
	
}

 botInfo_t* afiBotManager::FindBotProfileByIndex(int clientNum) {
	 botInfo_t* returnProfile = NULL;
	 int numProfiles = 0;
	 if(clientNum > MAX_CLIENTS) {
		 return returnProfile;
	 }

	 numProfiles = loadedBots.Num();
	 for(unsigned int iProfile = 0; iProfile < numProfiles; ++iProfile) {
		 if(loadedBots[iProfile]->clientNum == clientNum) {
			 returnProfile = loadedBots[iProfile];
			 break;
		 }
	 }
	 return returnProfile;
 }

 idEntity* afiBotManager::GetFlag(int team) {
#ifdef CTF
	 idEntity* theFlag = gameLocal.mpGame.GetTeamFlag(team);
	return theFlag;
#endif

}

 int afiBotManager::GetFlagStatus(int team) {

#ifdef CTF
	 return gameLocal.mpGame.GetFlagStatus(team);
#endif
 }

 void afiBotManager::AddBotInfo(botInfo_t* newBotInfo) {
	loadedBots.Append(newBotInfo);
}

 void afiBotManager::UpdateUserInfo( void ) {
	assert( !gameLocal.isClient );
	for ( int i = MAX_CLIENTS - 1; i >= 0; i-- ) {
		if ( gameLocal.entities[i] && gameLocal.entities[i]->IsType( afiBotPlayer::Type ) ) {
			cmdSystem->BufferCommandText( CMD_EXEC_APPEND, va( "updateUI %d\n", i ) );
		}
	}
}

 void afiBotManager::Cmd_AddBot_f( const idCmdArgs& args ) {
	if ( gameLocal.isClient ) { // !gameLocal.isServer isn't valid soon enough for some reason
		gameLocal.Printf( "Bots may only be added on server\n" );
		return;
	}
	
	// Detour addbot commands for next idGameLocal::InitFromMapRestart,
	//    the Clients will be ready at that point
	idStr classname = args.Argv(1);
	botInfo_t* botProfile = NULL;
	botProfile = FindBotProfile(classname);
	if(botProfile == NULL) {
		gameLocal.Warning(va("No Loaded Bot Profile Named: %s \n",classname));
		return;
	}

	if ( gameLocal.GameState() != GAMESTATE_ACTIVE ) {
		if(numQueBots < MAX_CLIENTS) {
			common->Printf( "QUEUE SUCCESS: Adding Bot to Que\n" );
			cmdQue[numQueBots] = args;
			numQueBots++;
		}
		else
			common->Printf( "QUEUE FAILED: Max Bots in Queue\n" );
		return;
	}

	int numClients = 0;
	for ( int i = 0; i < MAX_CLIENTS; i++ ){
		if ( gameLocal.entities[i] ) {
			numClients++;
		}
	}

	if ( numClients >= gameLocal.serverInfo.GetInt( "si_maxPlayers" ) ) {
		gameLocal.Printf( "Server is full\n" );
		return;
	}

	//If we have gotten down here the server isn't full and the game is running so we
	//can add the bot.
	AddBot( args );
}

 void afiBotManager::AddBot( const idCmdArgs& args ) {
	idStr classname = args.Argv( 1 );
	if ( !classname.Length() ) {
		gameLocal.Printf( "No bot def specified." );
		return;
	}

	const idDeclEntityDef *botDef = gameLocal.FindEntityDef( classname.c_str(), false );
	if ( !botDef ) {
		if ( classname.Length() ) {
			gameLocal.Printf( "Unknown bot def '%s'.", classname.c_str() );
		}
		return;
	}

	//Start fake client connect
	int clientNum = networkSystem->ServerConnectBot();
	if ( clientNum == -1 ) {
		gameLocal.Printf( "No available slot for bot.\n" );
		return;
	}

	persistArgs[clientNum] = args;
	botSpawned[clientNum] = true;

	// Index num of the bots def is saved so it can be sent to clients in order to spawn the right bot class
	SetBotDefNumber( clientNum, botDef->Index() );

	//This function calls spawnBot instead of spawnPlayer, which actually creates the
	//afiBotPlayer and afiBotBrain, and generates the proper linkage.
	gameLocal.ServerClientBegin(clientNum);

	gameLocal.Printf( "Bot added.\n" );
}

 void afiBotManager::Cmd_RemoveBot_f( const idCmdArgs& args ) {
	if ( !gameLocal.isMultiplayer ) {
		gameLocal.Printf( "This isn't multiplayer, so there no bots to remove, so yeah, you're mental.\n" );
		return;
	}

	if ( !gameLocal.isServer ) {
		gameLocal.Printf( "Bots may only be removed on server, only it has the powah!\n" );
		return;
	}

	idPlayer* player = gameLocal.GetClientByCmdArgs( args );
	if ( !player ) {
		gameLocal.Printf( "usage: removeBot <client nickname> or removeBot <client index>\n" );
		return;
	}
	if ( player && player->IsType( afiBotPlayer::Type ) ) {
		RemoveBot( player->entityNumber );
	} else {
		gameLocal.Printf( "There is no spoon, I mean, bot..." );
		return;
	}
}

 void afiBotManager::Cmd_RemoveAllBots_f( const idCmdArgs & args ) {
	if ( !gameLocal.isMultiplayer ) {
		gameLocal.Printf( "RemoveAllBots can only be used in a multiplayer game\n" );
		return;
	}

	if ( gameLocal.isClient ) {
		gameLocal.Printf( "You have no such power. This is a server command\n" );
		return;
	}

	for ( int i = 0; i < MAX_CLIENTS; i++ ) {
		RemoveBot( i );
	}
}

 void afiBotManager::DropABot( void ) {
	if ( !gameLocal.isMultiplayer ) {
		gameLocal.Printf( "DropABot can only be used in a multiplayer game\n" );
		return;
	}

	if ( gameLocal.isClient ) {
		gameLocal.Printf( "You have no such power. This is a server command\n" );
		return;
	}

	//Remove the first bot we find.
	for ( int i = 0; i < MAX_CLIENTS; i++ ) {
		if ( gameLocal.entities[i] && gameLocal.entities[i]->IsType( afiBotPlayer::Type ) ) {
			RemoveBot( i );
			break;
		}
	}
}

 void afiBotManager::RemoveBot( int clientNum ) {
	//persistArgs[ clientNum ].Clear();
	//botSpawned[ clientNum ] = false;
	if ( gameLocal.entities[ clientNum ] && gameLocal.entities[ clientNum ]->IsType( afiBotPlayer::Type ) ) {
		cmdSystem->BufferCommandText( CMD_EXEC_NOW, va( "say Removing Bot '%s^0'\n", gameLocal.userInfo[ clientNum ].GetString( "ui_name" ) ) );
		cmdSystem->BufferCommandText( CMD_EXEC_NOW, va( "kick %d\n", gameLocal.entities[ clientNum ]->entityNumber ) );
	}
}

 int afiBotManager::IsClientBot( int clientNum ) {
	return botSpawned[clientNum];
}

 void afiBotManager::SetBotDefNumber( int clientNum, int botDefNumber ) {
	botEntityDefNumber[clientNum] = botDefNumber;
}

/*
===================
afiBotManager::GetBotDefNumber
Gets the bot's entityDef number
===================
*/
int afiBotManager::GetBotDefNumber( int clientNum ) {
	return botEntityDefNumber[clientNum];
}

/*
===================
afiBotManager::GetBotClassname
Gets the bot's classname
===================
*/
idStr afiBotManager::GetBotClassname( int clientNum ) {
	return (idStr)declManager->DeclByIndex( DECL_ENTITYDEF, botEntityDefNumber[clientNum], false )->GetName();//botDeclName;
}

/*
===================
afiBotManager::SpawnBot
Equivalent of gamelocal spawnplayer except it loads from cmdargs
===================
*/
void afiBotManager::SpawnBot( int clientNum ) {
	idDict spawnDict;
	idStr classname;
	afiBotPlayer * playerBody;
	afiBotBrain* brain;

	gameLocal.DPrintf( "SpawnBot: %i\n", clientNum );

	classname = GetBotClassname( clientNum );
	if ( !classname.Length() ) {
		gameLocal.Warning( "Unknown classname '%s', defaulting to 'bot'.", classname );
		classname = "bot";
	}

	//Default setup of spawnDict if .def file is not filled out.
	spawnDict.SetInt( "spawn_entnum", clientNum );
	spawnDict.Set( "name", va( "bot%d", clientNum ) );
	spawnDict.Set( "classname", classname ); 
	spawnDict.SetBool( "hide", false );

	//Finding the loaded entityDef for the class.
	const idDict* botEntityDict = gameLocal.FindEntityDefDict( classname, false );
	if ( !botEntityDict ) {
		if ( classname ) {
			gameLocal.Error( "Unknown classname '%s'.", classname );
		}
		return;
	}

	//Copying those key/value pairs from the loaded entityDef over the default.
	spawnDict.Copy( *botEntityDict );

	// key/values passed from cmd args
	if ( gameLocal.isServer ) {
		const char *key, *value;
		const idCmdArgs * cmdArgs = GetPersistArgs( clientNum );
		if ( cmdArgs ) {
			for( int i = 2; i < cmdArgs->Argc() - 1; i += 2 ) {
				key = cmdArgs->Argv( i );
				value = cmdArgs->Argv( i + 1 );
				spawnDict.Set( key, value );
			}
		}
	}


	//Spawn the afiBotPlayer from the entityDef
	//Grab the name of the bot from the loaded entityDef
	idStr botName = spawnDict.GetString("scriptclass");
	botInfo_t* botProfile = FindBotProfile(botName);

	//Must be called the for the CallSpawn() function to operate properly.
	//Game spawns one entity at a time and uses single spawnArgs to spawn the entity.
	gameLocal.SetSpawnArgs(spawnDict);
	
	if( botProfile ) {
		botProfile->clientNum = clientNum;
		//If we are a script bot then the bot instances will be created from python.
		if(botProfile->botType == SCRIPT) {
			brain = SpawnBrain(botName,clientNum);

			object botPlayerClass = gameLocal.globalNamespace["afiBotPlayer"];
			brain->scriptBody = botPlayerClass();
			playerBody = extract<afiBotPlayer*>(ptr(brain->scriptBody));

			if ( clientNum >= gameLocal.numClients ) {
				gameLocal.numClients = clientNum + 1; 
			}

			//Create a boost python dictionary
			brain->botDict = dict();

			//Copy from our dictonary to python dictonary
			//now this may be a little finiky because all values are stored as
			//strings, and there is no way with simple parsing to determine the
			//actual dataType of the value stored, so it is up to the user to know
			//and use the appropriate library function to translate (i.e. atof(), atoi() etc.)
			int numPairs = spawnDict.GetNumKeyVals();
			for( int iPair = 0; iPair < numPairs; ++iPair ) {
				const idKeyValue* keyVal = spawnDict.GetKeyVal(iPair);
				brain->botDict[keyVal->GetKey().c_str()] = keyVal->GetValue().c_str();
			}

			//Necessary part of entity spawning process to initialize all the variables
			//from the hierarchy.
			playerBody->CallSpawn();
		
			//Link all the necessary components between brain and body.
			brain->SetBody( playerBody );
			brain->SetPhysics( ( idPhysics_Player* )playerBody->GetPhysics() );
			playerBody->SetAAS();
			brain->SetAAS();
			playerBody->SetBrain(brain);
			//Call the script spawn function after entity has been created.
			brain->Spawn();
		}
	}
	//Let the networked game know that a bot spawned.
	gameLocal.mpGame.SpawnPlayer( clientNum );
}

afiBotBrain* afiBotManager::SpawnBrain(idStr botName,int clientNum) {
	afiBotBrain*	returnBrain = NULL;
	botInfo_t*		loadedBotProfile = NULL;

	loadedBotProfile = FindBotProfile(botName);

	if( loadedBotProfile != NULL ) {

		if( 0 == botName.Cmp(loadedBotProfile->botName.c_str()) ) {
			try {
				loadedBotProfile->scriptInstances[clientNum] = loadedBotProfile->botClassInstance();
				returnBrain = extract<afiBotBrain*>(loadedBotProfile->scriptInstances[clientNum].ptr());
			}
			catch(...) {
				gameLocal.HandlePythonError();
			}
			
			//Place a reference to the spawnedBrain in the fast list
			//so we can easily dispatch event functions later on
			brainFastList[clientNum] = returnBrain;

		}
	}

	return returnBrain;
}

botInfo_t*  afiBotManager::FindBotProfile(idStr botName) {
	botInfo_t* botProfile = NULL;
	int numLoadedBots;
	int iBotProfile = 0;

	numLoadedBots = loadedBots.Num();
	for(iBotProfile = 0; iBotProfile < numLoadedBots; ++iBotProfile) {
		idStr loadedName = loadedBots[iBotProfile]->botName;

		if(0 == botName.Icmp(loadedName.c_str()) ) {
			botProfile = loadedBots[iBotProfile];
			return botProfile;
		}
	}
	return botProfile;
}

void afiBotManager::ProcessChat(const char* text) {


}
/*
===================
afiBotManager::OnDisconnect
===================
*/
void afiBotManager::OnDisconnect( int clientNum ) {
	assert( clientNum >= 0 && clientNum < MAX_CLIENTS );
	botSpawned[ clientNum ] = false;
	memset( &botCmds[ clientNum ], 0, sizeof( usercmd_t ) );
	botEntityDefNumber[ clientNum ] = -1;

	//TODO: Let other bots know about the disconnect of a player.

}


/*
===================
afiBotManager::InitBotsFromMapRestart
Connect all qued bots to the game or refresh current bots
===================
*/
void afiBotManager::InitBotsFromMapRestart( void ) {
	if ( !gameLocal.isServer ) return;

	//This function gets called on map load.
	//All qued up bots are added. The Queueing of bots allows
	//us to tackle two birds with one stone. This allows us to
	//setup a match before we actually enter the game, and handles
	//if we should switch maps on a running server and need to reload bots.
	gameLocal.Printf( "***Starting Bot Refreshes\n" );
	for ( int botID = 0; botID < MAX_CLIENTS; botID++ ) {
		if ( IsClientBot( botID ) ) {
			common->Printf( "***Adding existing Bot to Que\n" );
			cmdQue[numQueBots] = persistArgs[botID];
			persistArgs[botID].Clear();
			botSpawned[botID] = false;
			numQueBots++;
		}
	}

	// Add bots from command line or config Que - 
	if ( numQueBots ) {
		for ( int i = 0; i < MAX_CLIENTS; i++ ) {
			botSpawned[i] = false;
		}
		for ( int i = 0; i < numQueBots; i++ ) {
			common->Printf( "***Adding Bot %i from Que\n", i );
			Cmd_AddBot_f( cmdQue[i] );
			cmdQue[i].Clear();
		}
		numQueBots = 0;
		return;
	}
}

/*
=====================
afiBotManager::GetPersistArgs
=====================
*/
idCmdArgs *	afiBotManager::GetPersistArgs( int clientNum ) {
	idCmdArgs * args = &(persistArgs[ clientNum ]);
	if ( args->Argc() ) {
		return args;
	} else {
		return NULL;
	}
}

/*
===================
afiBotManager::GetUserCmd
===================
*/
usercmd_t * afiBotManager::GetUserCmd( int clientNum ) {
	assert( clientNum >= 0 && clientNum < MAX_CLIENTS );
	return &( botCmds[clientNum] );
}

/*
===================
afiBotManager::SetUserCmd
===================
*/
void afiBotManager::SetUserCmd( int clientNum, usercmd_t * usrCmd ) {
	assert( clientNum >= 0 && clientNum < MAX_CLIENTS );

	botCmds[clientNum].angles[0] = usrCmd->angles[0];
	botCmds[clientNum].angles[1] = usrCmd->angles[1];
	botCmds[clientNum].angles[2] = usrCmd->angles[2];
	botCmds[clientNum].forwardmove = usrCmd->forwardmove;
	botCmds[clientNum].rightmove = usrCmd->rightmove;
	botCmds[clientNum].upmove =	usrCmd->upmove;
	botCmds[clientNum].buttons = usrCmd->buttons;
	botCmds[clientNum].impulse = usrCmd->impulse;
}

/*
===================
afiBotManager::afiBotManager
===================
*/
afiBotManager::afiBotManager() {
}

/*
===================
afiBotManager::~afiBotManager
===================
*/
afiBotManager::~afiBotManager() {

	Shutdown();
}

#endif