using Newtonsoft.Json;
using System;
using System.Linq;
using System.Linq.Expressions;
using System.Reflection;

namespace BigSisterNodeNet.Core.Extensions
{
    /// <summary>
    /// Extensions pour récupérer les noms de propriétés JSON
    /// </summary>
    public static class PropertyExtensions
    {
        /// <summary>
        /// Récupère le nom JSON d'une propriété (via l'attribut JsonProperty) ou le nom de la propriété si non trouvé
        /// </summary>
        /// <typeparam name="TSource">Type source</typeparam>
        /// <typeparam name="TProperty">Type de la propriété</typeparam>
        /// <param name="source">Instance source (peut être null)</param>
        /// <param name="propertyExpression">Expression lambda pointant vers la propriété</param>
        /// <returns>Le nom JSON de la propriété ou le nom de la propriété C# si JsonProperty n'est pas trouvé</returns>
        /// <example>
        /// var jsonName = this.GetJsonPropertyName(x => x.InstrumentName); // Retourne "instrumentName"
        /// </example>
        public static string GetJsonPropertyName<TSource, TProperty>(
            this TSource source,
            Expression<Func<TSource, TProperty>> propertyExpression)
        {
            var memberExpression = GetMemberExpression(propertyExpression);
            if (memberExpression != null)
            {
                var propertyInfo = memberExpression.Member as PropertyInfo;
                if (propertyInfo != null)
                {
                    // Si source est fourni, chercher l'attribut sur le type concret
                    if (source != null)
                    {
                        var concreteType = source.GetType();
                        var concreteProperty = concreteType.GetProperty(propertyInfo.Name);
                        if (concreteProperty != null)
                        {
                            var jsonProperty = concreteProperty.GetCustomAttribute<JsonPropertyAttribute>();
                            return jsonProperty?.PropertyName ?? propertyInfo.Name;
                        }
                    }

                    // Sinon, chercher sur le type de l'expression
                    var jsonPropertyAttr = propertyInfo.GetCustomAttribute<JsonPropertyAttribute>();
                    return jsonPropertyAttr?.PropertyName ?? propertyInfo.Name;
                }
            }

            throw new ArgumentException("L'expression doit pointer vers une propriété", nameof(propertyExpression));
        }

        /// <summary>
        /// Récupère le nom JSON d'une propriété depuis son type (méthode statique)
        /// </summary>
        /// <typeparam name="TSource">Type source</typeparam>
        /// <typeparam name="TProperty">Type de la propriété</typeparam>
        /// <param name="propertyExpression">Expression lambda pointant vers la propriété</param>
        /// <returns>Le nom JSON de la propriété ou le nom de la propriété C# si JsonProperty n'est pas trouvé</returns>
        /// <example>
        /// var jsonName = PropertyExtensions.GetJsonPropertyName&lt;Node, string&gt;(x => x.InstrumentName);
        /// </example>
        public static string GetJsonPropertyName<TSource, TProperty>(
            Expression<Func<TSource, TProperty>> propertyExpression)
        {
            var memberExpression = GetMemberExpression(propertyExpression);
            if (memberExpression != null)
            {
                var propertyInfo = memberExpression.Member as PropertyInfo;
                if (propertyInfo != null)
                {
                    var jsonProperty = propertyInfo.GetCustomAttribute<JsonPropertyAttribute>();
                    return jsonProperty?.PropertyName ?? propertyInfo.Name;
                }
            }

            throw new ArgumentException("L'expression doit pointer vers une propriété", nameof(propertyExpression));
        }

        /// <summary>
        /// Extrait le MemberExpression d'une expression, gérant les conversions implicites
        /// </summary>
        private static MemberExpression GetMemberExpression<TSource, TProperty>(
            Expression<Func<TSource, TProperty>> propertyExpression)
        {
            // Cas direct : x => x.Property
            if (propertyExpression.Body is MemberExpression memberExpression)
            {
                return memberExpression;
            }

            // Cas avec conversion : x => (object)x.Property ou x => x.Property.ToString()
            if (propertyExpression.Body is UnaryExpression unaryExpression)
            {
                if (unaryExpression.Operand is MemberExpression operandMember)
                {
                    return operandMember;
                }
            }

            // Cas avec appel de méthode : x => x.Property.ToString()
            if (propertyExpression.Body is MethodCallExpression methodCall)
            {
                if (methodCall.Object is MemberExpression methodMember)
                {
                    return methodMember;
                }
            }

            return null;
        }
    }
}
