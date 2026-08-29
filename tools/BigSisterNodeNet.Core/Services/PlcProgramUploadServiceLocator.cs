using System;

namespace BigSisterNodeNet.Core.Services
{
    public static class PlcProgramUploadServiceLocator
    {
        private static IPlcProgramUploadService _instance;

        public static void SetInstance(IPlcProgramUploadService service)
        {
            _instance = service ?? throw new ArgumentNullException(nameof(service));
        }

        public static IPlcProgramUploadService Current => _instance;

        public static void Reset()
        {
            _instance = null;
        }
    }
}