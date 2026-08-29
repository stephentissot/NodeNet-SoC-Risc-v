using BigSisterNodeNet.Core.Instruments;
using BigSisterNodeNet.Core.PlcCore;

namespace BigSisterNodeNet.Core.Services
{
    public interface IPointCatalogService
    {
        void RequestPointDefinitions(NodeNet_SOC node, string path = "", int offset = 0, int limit = 100);
        void RequestPointStates(NodeNet_SOC node, string path = "", int offset = 0, int limit = 100);
        void UpsertPointDefinition(NodeNet_SOC node, PointDefinition definition);
        void HandlePointDefinitionsResponse(PointDefinitionsResponse response);
        void HandlePointStatesResponse(PointStatesResponse response);
    }
}
