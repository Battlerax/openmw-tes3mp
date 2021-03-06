//
// Created by koncord on 05.01.16.
//

#ifndef OPENMW_PLAYER_HPP
#define OPENMW_PLAYER_HPP

#include <map>
#include <string>
#include <chrono>
#include <RakNetTypes.h>

#include <components/esm/npcstats.hpp>
#include <components/esm/cellid.hpp>
#include <components/esm/loadnpc.hpp>
#include <components/esm/loadcell.hpp>

#include <components/openmw-mp/Log.hpp>
#include <components/openmw-mp/Base/BasePlayer.hpp>
#include <components/openmw-mp/Packets/Player/PlayerPacket.hpp>
#include "Cell.hpp"
#include "CellController.hpp"

struct Player;
typedef std::map<RakNet::RakNetGUID, Player*> TPlayers;
typedef std::map<unsigned short, Player*> TSlots;

class Players
{
public:
    static void newPlayer(RakNet::RakNetGUID guid);
    static void deletePlayer(RakNet::RakNetGUID guid);
    static Player *getPlayer(RakNet::RakNetGUID guid);
    static Player *getPlayer(unsigned short id);
    static TPlayers *getPlayers();
    static unsigned short getLastPlayerId();

private:
    static TPlayers players;
    static TSlots slots;
};

class Player : public mwmp::BasePlayer
{
    friend class Cell;
    unsigned short id;
public:

    enum
    {
        NOTLOADED=0,
        LOADED,
        POSTLOADED
    };
    Player(RakNet::RakNetGUID guid);

    unsigned short getId();
    void setId(unsigned short id);

    bool isHandshaked();
    void setHandshake();

    void setLoadState(int state);
    int getLoadState();

    virtual ~Player();

    CellController::TContainer *getCells();
    void sendToLoaded(mwmp::PlayerPacket *myPacket);

    void forEachLoaded(std::function<void(Player *pl, Player *other)> func);

public:
    mwmp::InventoryChanges inventoryChangesBuffer;
    mwmp::SpellbookChanges spellbookChangesBuffer;
    mwmp::JournalChanges journalChangesBuffer;
    mwmp::FactionChanges factionChangesBuffer;
    mwmp::TopicChanges topicChangesBuffer;
    mwmp::KillChanges killChangesBuffer;
    mwmp::BookChanges bookChangesBuffer;

private:
    CellController::TContainer cells;
    bool handshakeState;
    int loadState;

};

#endif //OPENMW_PLAYER_HPP
