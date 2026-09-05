using BigSisterNodeNet.Core.Instruments;
using BigSisterNodeNet.Plc;

namespace BigSisterNodeNet.Core.Services
{
    public interface IPlcProgramUploadService
    {
        PlcUploadResult UploadProgram(NodeNet_SOC node, string program, ushort slot, PlcObjectFileOptions objectFileOptions = null);
        PlcDownloadResult DownloadProgramBytecode(NodeNet_SOC node, ushort slot);
        PlcDownloadResult DownloadProgramObjectFile(NodeNet_SOC node, ushort slot);
        PlcEraseResult EraseProgramSlot(NodeNet_SOC node, ushort slot);
    }
}