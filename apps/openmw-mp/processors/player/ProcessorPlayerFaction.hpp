#ifndef OPENMW_PROCESSORPLAYERFACTION_HPP
#define OPENMW_PROCESSORPLAYERFACTION_HPP

#include "apps/openmw-mp/PlayerProcessor.hpp"

namespace mwmp
{
    class ProcessorPlayerFaction : public PlayerProcessor
    {
    public:
        ProcessorPlayerFaction()
        {
            BPP_INIT(ID_PLAYER_FACTION)
        }

        void Do(PlayerPacket &packet, Player &player) override
        {
            DEBUG_PRINTF(strPacketID.c_str());

            packet.Send(true);

            Script::Call<Script::CallbackIdentity("OnPlayerFaction")>(player.getId());
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERFACTION_HPP
