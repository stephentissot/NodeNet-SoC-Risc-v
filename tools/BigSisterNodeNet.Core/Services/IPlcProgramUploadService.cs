using BigSisterNodeNet.Core.Instruments;
using BigSisterNodeNet.Plc;

namespace BigSisterNodeNet.Core.Services
{
    public interface IPlcProgramUploadService
    {
        PlcUploadResult UploadProgram(NodeNet_SOC node, string program, ushort slot, PlcObjectFileOptions objectFileOptions = null);
    }
}