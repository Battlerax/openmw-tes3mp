#ifndef OPENMW_PROCESSORVIDEOPLAY_HPP
#define OPENMW_PROCESSORVIDEOPLAY_HPP

#include "apps/openmw-mp/WorldProcessor.hpp"

namespace mwmp
{
    class ProcessorVideoPlay : public WorldProcessor
    {
    public:
        ProcessorVideoPlay()
        {
            BPP_INIT(ID_VIDEO_PLAY)
        }
    };
}

#endif //OPENMW_PROCESSORVIDEOPLAY_HPP
