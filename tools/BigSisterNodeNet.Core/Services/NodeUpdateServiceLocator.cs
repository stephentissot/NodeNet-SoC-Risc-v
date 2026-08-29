using System;

namespace BigSisterNodeNet.Core.Services
{
    /// <summary>
    /// Service Locator statique pour INodeUpdateService
    /// Permet aux objets créés par désérialisation JSON d'accéder au service
    /// </summary>
    public static class NodeUpdateServiceLocator
    {
        private static INodeUpdateService _instance;

        /// <summary>
        /// Configure le service global
        /// Doit être appelé au démarrage de l'application
        /// </summary>
        public static void SetInstance(INodeUpdateService service)
        {
            _instance = service ?? throw new ArgumentNullException(nameof(service));
        }

        /// <summary>
        /// Obtient l'instance du service
        /// </summary>
        public static INodeUpdateService Current
        {
            get
            {
                if (_instance == null)
                {
                    // En mode développement/test, retourner null au lieu de lever une exception
                    return null;
                }
                return _instance;
            }
        }

        /// <summary>
        /// Réinitialise le service (utile pour les tests)
        /// </summary>
        public static void Reset()
        {
            _instance = null;
        }
    }
}
