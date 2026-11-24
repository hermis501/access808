// Connects to REAPER through its VST3 host extension, so the editor can speak
// through OSARA (the user's own screen reader voice) instead of a SAPI voice.
// Kept in its own file so the VST3 SDK headers are included in one place.

#include "PluginProcessor.h"

JUCE_BEGIN_IGNORE_WARNINGS_MSVC (4100)
#include <pluginterfaces/base/funknown.h>
JUCE_END_IGNORE_WARNINGS_MSVC

namespace
{
    // From Cockos' reaper_vst3_interfaces.h (REAPER 5.02+): the host object
    // REAPER passes to setIHostApplication implements this.
    class IReaperHostApplication : public Steinberg::FUnknown
    {
    public:
        virtual void* PLUGIN_API getReaperApi (Steinberg::CStringA funcname) = 0;
        virtual void* PLUGIN_API getReaperParent (Steinberg::uint32 w) = 0;
        virtual void* PLUGIN_API reaperExtended (Steinberg::uint32 call, void* parm1, void* parm2, void* parm3) = 0;

        static const Steinberg::FUID iid;
    };

    DECLARE_CLASS_IID (IReaperHostApplication, 0x79655E36, 0x77EE4267, 0xA573FEF7, 0x4912C27C)
    DEF_CLASS_IID (IReaperHostApplication)
}

void Access808Processor::setIHostApplication (Steinberg::FUnknown* hostContext)
{
    osaraOutputMessage = nullptr;
    if (hostContext == nullptr)
        return;

    void* obj = nullptr;
    if (hostContext->queryInterface (IReaperHostApplication::iid, &obj) != Steinberg::kResultOk || obj == nullptr)
        return;

    auto* reaper = static_cast<IReaperHostApplication*> (obj);
    // Registered by OSARA as "API_osara_outputMessage"; null without OSARA.
    osaraOutputMessage = reinterpret_cast<OsaraOutputMessage> (reaper->getReaperApi ("osara_outputMessage"));
    reaper->release();
}
