//
// Created by koncord on 12.01.16.
//

#include "Player.hpp"
#include "ProcessorInitializer.hpp"
#include <RakPeer.h>
#include <Kbhit.h>
#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/Log.hpp>
#include <iostream>
#include <Script/Script.hpp>
#include <Script/API/TimerAPI.hpp>
#include <chrono>
#include <thread>

#include "Networking.hpp"
#include "MasterClient.hpp"
#include "Cell.hpp"
#include "CellController.hpp"
#include "PlayerProcessor.hpp"
#include "ActorProcessor.hpp"
#include "WorldProcessor.hpp"
#include <components/openmw-mp/Version.hpp>
#include <components/openmw-mp/Packets/PacketPreInit.hpp>

using namespace mwmp;
using namespace std;

Networking *Networking::sThis = 0;

static int currentMpNum = 0;

Networking::Networking(RakNet::RakPeerInterface *peer) : mclient(nullptr)
{
    sThis = this;
    this->peer = peer;
    players = Players::getPlayers();

    CellController::create();

    playerPacketController = new PlayerPacketController(peer);
    actorPacketController = new ActorPacketController(peer);
    worldPacketController = new WorldPacketController(peer);

    // Set send stream
    playerPacketController->SetStream(0, &bsOut);
    actorPacketController->SetStream(0, &bsOut);
    worldPacketController->SetStream(0, &bsOut);

    running = true;
    exitCode = 0;

    Script::Call<Script::CallbackIdentity("OnServerInit")>();

    serverPassword = TES3MP_DEFAULT_PASSW;

    ProcessorInitializer();
}

Networking::~Networking()
{
    Script::Call<Script::CallbackIdentity("OnServerExit")>(false);

    CellController::destroy();

    sThis = 0;
    delete playerPacketController;
    delete actorPacketController;
    delete worldPacketController;
    LOG_QUIT();
}

void Networking::setServerPassword(std::string passw) noexcept
{
    serverPassword = passw.empty() ? TES3MP_DEFAULT_PASSW : passw;
}

bool Networking::isPassworded() const
{
    return serverPassword != TES3MP_DEFAULT_PASSW;
}

void Networking::processPlayerPacket(RakNet::Packet *packet)
{
    Player *player = Players::getPlayer(packet->guid);

    PlayerPacket *myPacket = playerPacketController->GetPacket(packet->data[0]);

    if (packet->data[0] == ID_HANDSHAKE)
    {
        myPacket->setPlayer(player);
        myPacket->Read();

        if (player->isHandshaked())
        {
            LOG_MESSAGE_SIMPLE(Log::LOG_WARN, "Wrong handshake with player %d, name: %s", player->getId(),
                               player->npc.mName.c_str());
            kickPlayer(player->guid);
            return;
        }

        if (player->passw != serverPassword)
        {
            LOG_MESSAGE_SIMPLE(Log::LOG_WARN, "Wrong server password for player %d, name: %s (pass: %s)",
                               player->getId(), player->npc.mName.c_str(), player->passw.c_str());
            kickPlayer(player->guid);
            return;
        }
        player->setHandshake();
        return;
    }

    if (!player->isHandshaked())
    {
        LOG_MESSAGE_SIMPLE(Log::LOG_WARN, "Have not completed handshake with player %d", player->getId());
        //KickPlayer(player->guid);
        return;
    }

    if (packet->data[0] == ID_LOADED)
    {
        player->setLoadState(Player::LOADED);

        static constexpr unsigned int ident = Script::CallbackIdentity("OnPlayerConnect");
        Script::CallBackReturn<ident> result = true;
        Script::Call<ident>(result, Players::getPlayer(packet->guid)->getId());

        if (!result)
        {
            playerPacketController->GetPacket(ID_USER_DISCONNECTED)->setPlayer(Players::getPlayer(packet->guid));
            playerPacketController->GetPacket(ID_USER_DISCONNECTED)->Send(false);
            Players::deletePlayer(packet->guid);
            return;
        }
    }
    else if (packet->data[0] == ID_PLAYER_BASEINFO)
    {
        LOG_MESSAGE_SIMPLE(Log::LOG_INFO, "Received ID_PLAYER_BASEINFO about %s", player->npc.mName.c_str());

        myPacket->setPlayer(player);
        myPacket->Read();
        myPacket->Send(true);
    }

    if (player->getLoadState() == Player::NOTLOADED)
        return;
    else if (player->getLoadState() == Player::LOADED)
    {
        player->setLoadState(Player::POSTLOADED);
        newPlayer(packet->guid);
        return;
    }


    if (!PlayerProcessor::Process(*packet))
        LOG_MESSAGE_SIMPLE(Log::LOG_WARN, "Unhandled PlayerPacket with identifier %i has arrived", packet->data[0]);

}

void Networking::processActorPacket(RakNet::Packet *packet)
{
    Player *player = Players::getPlayer(packet->guid);

    if (!player->isHandshaked() || player->getLoadState() != Player::POSTLOADED)
        return;

    if (!ActorProcessor::Process(*packet, baseActorList))
        LOG_MESSAGE_SIMPLE(Log::LOG_WARN, "Unhandled ActorPacket with identifier %i has arrived", packet->data[0]);

}

void Networking::processWorldPacket(RakNet::Packet *packet)
{
    Player *player = Players::getPlayer(packet->guid);

    if (!player->isHandshaked() || player->getLoadState() != Player::POSTLOADED)
        return;

    if (!WorldProcessor::Process(*packet, baseEvent))
        LOG_MESSAGE_SIMPLE(Log::LOG_WARN, "Unhandled WorldPacket with identifier %i has arrived", packet->data[0]);

}

void Networking::update(RakNet::Packet *packet)
{
    Player *player = Players::getPlayer(packet->guid);

    RakNet::BitStream bsIn(&packet->data[1], packet->length, false);

    bsIn.IgnoreBytes((unsigned int) RakNet::RakNetGUID::size()); // Ignore GUID from received packet

    if (player == 0)
    {
        if (packet->data[0] == ID_GAME_PREINIT)
        {
            DEBUG_PRINTF("ID_GAME_PREINIT");
            PacketPreInit::PluginContainer plugins;

            PacketPreInit packetPreInit(peer);
            packetPreInit.SetReadStream(&bsIn);
            packetPreInit.setChecksums(&plugins);
            packetPreInit.Read();

            auto plugin = plugins.begin();
            if (samples.size() == plugins.size())
            {
                for (int i = 0; plugin != plugins.end(); plugin++, i++)
                {
                    LOG_APPEND(Log::LOG_VERBOSE, "- %X\t%s", plugin->second[0], plugin->first.c_str());
                    if (samples[i].first == plugin->first) // if name is correct
                    {
                        auto &hashList = samples[i].second;
                        if (hashList.empty()) // and server do not allow to have custom hash for plugin
                            continue;
                        auto it = find(hashList.begin(), hashList.end(), plugin->second[0]);
                        if (it == hashList.end()) // hash not found in sample
                            break;

                    }
                    else // name is incorrect
                        break;
                }
            }
            RakNet::BitStream bs;
            packetPreInit.SetSendStream(&bs);
            if (plugin != plugins.end()) // if condition is true, then client have wrong plugin list
            {
                packetPreInit.setChecksums(&samples);
                packetPreInit.Send(packet->systemAddress);
                peer->CloseConnection(packet->systemAddress, true);
            }
            else
            {
                PacketPreInit::PluginContainer tmp;
                packetPreInit.setChecksums(&tmp);
                packetPreInit.Send(packet->systemAddress);
            }
            return;
        }

        playerPacketController->SetStream(&bsIn, 0);

        playerPacketController->GetPacket(ID_HANDSHAKE)->RequestData(packet->guid);
        Players::newPlayer(packet->guid);
        player = Players::getPlayer(packet->guid);
        return;
    }
    else if (playerPacketController->ContainsPacket(packet->data[0]))
    {
        playerPacketController->SetStream(&bsIn, 0);
        processPlayerPacket(packet);
    }
    else if (actorPacketController->ContainsPacket(packet->data[0]))
    {
        actorPacketController->SetStream(&bsIn, 0);
        processActorPacket(packet);
    }
    else if (worldPacketController->ContainsPacket(packet->data[0]))
    {
        worldPacketController->SetStream(&bsIn, 0);
        processWorldPacket(packet);
    }
    else
        LOG_MESSAGE_SIMPLE(Log::LOG_WARN, "Unhandled RakNet packet with identifier %i has arrived", packet->data[0]);
}

void Networking::newPlayer(RakNet::RakNetGUID guid)
{
    playerPacketController->GetPacket(ID_PLAYER_BASEINFO)->RequestData(guid);
    playerPacketController->GetPacket(ID_PLAYER_STATS_DYNAMIC)->RequestData(guid);
    playerPacketController->GetPacket(ID_PLAYER_POSITION)->RequestData(guid);
    playerPacketController->GetPacket(ID_PLAYER_CELL_CHANGE)->RequestData(guid);
    playerPacketController->GetPacket(ID_PLAYER_EQUIPMENT)->RequestData(guid);

    LOG_MESSAGE_SIMPLE(Log::LOG_WARN, "Sending info about other players to %lu", guid.g);

    for (TPlayers::iterator pl = players->begin(); pl != players->end(); pl++) //sending other players to new player
    {
        // If we are iterating over the new player, don't send the packets below
        if (pl->first == guid) continue;

        // If an invalid key makes it into the Players map, ignore it
        else if (pl->first == RakNet::UNASSIGNED_RAKNET_GUID) continue;

        // if player not fully connected
        else if (pl->second == nullptr) continue;

        // If we are iterating over a player who has inputted their name, proceed
        else if (pl->second->getLoadState() == Player::POSTLOADED)
        {
            playerPacketController->GetPacket(ID_PLAYER_BASEINFO)->setPlayer(pl->second);
            playerPacketController->GetPacket(ID_PLAYER_STATS_DYNAMIC)->setPlayer(pl->second);
            playerPacketController->GetPacket(ID_PLAYER_ATTRIBUTE)->setPlayer(pl->second);
            playerPacketController->GetPacket(ID_PLAYER_SKILL)->setPlayer(pl->second);
            playerPacketController->GetPacket(ID_PLAYER_POSITION)->setPlayer(pl->second);
            playerPacketController->GetPacket(ID_PLAYER_CELL_CHANGE)->setPlayer(pl->second);
            playerPacketController->GetPacket(ID_PLAYER_EQUIPMENT)->setPlayer(pl->second);

            playerPacketController->GetPacket(ID_PLAYER_BASEINFO)->Send(guid);
            playerPacketController->GetPacket(ID_PLAYER_STATS_DYNAMIC)->Send(guid);
            playerPacketController->GetPacket(ID_PLAYER_ATTRIBUTE)->Send(guid);
            playerPacketController->GetPacket(ID_PLAYER_SKILL)->Send(guid);
            playerPacketController->GetPacket(ID_PLAYER_POSITION)->Send(guid);
            playerPacketController->GetPacket(ID_PLAYER_CELL_CHANGE)->Send(guid);
            playerPacketController->GetPacket(ID_PLAYER_EQUIPMENT)->Send(guid);
        }
    }

    LOG_APPEND(Log::LOG_WARN, "- Done");

}

void Networking::disconnectPlayer(RakNet::RakNetGUID guid)
{
    Player *player = Players::getPlayer(guid);
    if (!player)
        return;
    Script::Call<Script::CallbackIdentity("OnPlayerDisconnect")>(player->getId());

    playerPacketController->GetPacket(ID_USER_DISCONNECTED)->setPlayer(player);
    playerPacketController->GetPacket(ID_USER_DISCONNECTED)->Send(true);
    Players::deletePlayer(guid);
}

PlayerPacketController *Networking::getPlayerPacketController() const
{
    return playerPacketController;
}

ActorPacketController *Networking::getActorPacketController() const
{
    return actorPacketController;
}

WorldPacketController *Networking::getWorldPacketController() const
{
    return worldPacketController;
}

BaseActorList *Networking::getLastActorList()
{
    return &baseActorList;
}

BaseEvent *Networking::getLastEvent()
{
    return &baseEvent;
}

int Networking::getCurrentMpNum()
{
    return currentMpNum;
}

void Networking::setCurrentMpNum(int value)
{
    currentMpNum = value;
}

int Networking::incrementMpNum()
{
    currentMpNum++;
    Script::Call<Script::CallbackIdentity("OnMpNumIncrement")>(currentMpNum);
    return currentMpNum;
}

const Networking &Networking::get()
{
    return *sThis;
}


Networking *Networking::getPtr()
{
    return sThis;
}

PacketPreInit::PluginContainer Networking::getPluginListSample()
{
    PacketPreInit::PluginContainer pls;
    unsigned id = 0;
    while (true)
    {
        unsigned field = 0;
        auto name = "";
        Script::Call<Script::CallbackIdentity("OnRequestPluginList")>(name, id, field++);
        if (strlen(name) == 0)
            break;
        PacketPreInit::HashList hashList;
        while (true)
        {
            auto hash = "";
            Script::Call<Script::CallbackIdentity("OnRequestPluginList")>(hash, id, field++);
            if (strlen(hash) == 0)
                break;
            hashList.push_back((unsigned)stoul(hash));
        }
        pls.push_back({name, hashList});
        id++;
    }
    return pls;
}

void Networking::stopServer(int code)
{
    running = false;
    exitCode = code;
}

int Networking::mainLoop()
{
    RakNet::Packet *packet;

    while (running)
    {
        if (kbhit() && getch() == '\n')
            break;
        for (packet=peer->Receive(); packet; peer->DeallocatePacket(packet), packet=peer->Receive())
        {
            if (getMasterClient()->Process(packet))
                continue;

            switch (packet->data[0])
            {
                case ID_REMOTE_DISCONNECTION_NOTIFICATION:
                    LOG_MESSAGE_SIMPLE(Log::LOG_WARN, "Client at %s has disconnected", packet->systemAddress.ToString());
                    break;
                case ID_REMOTE_CONNECTION_LOST:
                    LOG_MESSAGE_SIMPLE(Log::LOG_WARN, "Client at %s has lost connection", packet->systemAddress.ToString());
                    break;
                case ID_REMOTE_NEW_INCOMING_CONNECTION:
                    LOG_MESSAGE_SIMPLE(Log::LOG_WARN, "Client at %s has connected", packet->systemAddress.ToString());
                    break;
                case ID_CONNECTION_REQUEST_ACCEPTED:    // client to server
                {
                    LOG_MESSAGE_SIMPLE(Log::LOG_WARN, "Our connection request has been accepted");
                    break;
                }
                case ID_NEW_INCOMING_CONNECTION:
                    LOG_MESSAGE_SIMPLE(Log::LOG_WARN, "A connection is incoming from %s", packet->systemAddress.ToString());
                    break;
                case ID_NO_FREE_INCOMING_CONNECTIONS:
                    LOG_MESSAGE_SIMPLE(Log::LOG_WARN, "The server is full");
                    break;
                case ID_DISCONNECTION_NOTIFICATION:
                    LOG_MESSAGE_SIMPLE(Log::LOG_WARN,  "Client at %s has disconnected", packet->systemAddress.ToString());
                    disconnectPlayer(packet->guid);
                    break;
                case ID_CONNECTION_LOST:
                    LOG_MESSAGE_SIMPLE(Log::LOG_WARN, "Client at %s has lost connection", packet->systemAddress.ToString());
                    disconnectPlayer(packet->guid);
                    break;
                case ID_CONNECTED_PING:
                case ID_UNCONNECTED_PING:
                    break;
                default:
                    update(packet);
                    break;
            }
        }
        TimerAPI::Tick();
        this_thread::sleep_for(chrono::milliseconds(1));
    }

    TimerAPI::Terminate();
    return exitCode;
}

void Networking::kickPlayer(RakNet::RakNetGUID guid)
{
    peer->CloseConnection(guid, true);
}

unsigned short Networking::numberOfConnections() const
{
    return peer->NumberOfConnections();
}

unsigned int Networking::maxConnections() const
{
    return peer->GetMaximumIncomingConnections();
}

int Networking::getAvgPing(RakNet::AddressOrGUID addr) const
{
    return peer->GetAveragePing(addr);
}

MasterClient *Networking::getMasterClient()
{
    return mclient;
}

void Networking::InitQuery(std::string queryAddr, unsigned short queryPort)
{
    mclient = new MasterClient(peer, queryAddr, queryPort);
}

void Networking::postInit()
{
    Script::Call<Script::CallbackIdentity("OnServerPostInit")>();
    samples = getPluginListSample();
    if (mclient)
    {
        for (auto plugin : samples)
        {
            if (!plugin.second.empty())
                mclient->PushPlugin({plugin.first, plugin.second[0]});
            else
                mclient->PushPlugin({plugin.first, 0});
        }
    }
}
