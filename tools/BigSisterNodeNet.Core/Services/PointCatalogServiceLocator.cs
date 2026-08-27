using System;

namespace BigSisterNodeNet.Core.Services
{
    public static class PointCatalogServiceLocator
    {
        private static IPointCatalogService _instance;

        public static void SetInstance(IPointCatalogService service)
        {
            _instance = service ?? throw new ArgumentNullException(nameof(service));
        }

        public static IPointCatalogService Current
        {
            get
            {
                return _instance;
            }
        }

        public static void Reset()
        {
            _instance = null;
        }
    }
}
