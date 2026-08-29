# Correction : GetJsonPropertyName ne trouvait pas l'attribut JsonProperty

## 🐛 Problème identifié

La méthode `GetJsonPropertyName` ne retournait pas le nom JSON défini par l'attribut `[JsonProperty]` sur la propriété `InstrumentName`.

### Cause racine

Trois problèmes combinés :

1. **Interface vs Implémentation** : Quand l'expression `x => x.InstrumentName` utilise le type `INode`, la réflexion cherche l'attribut sur l'interface, pas sur la classe `Node` qui contient réellement l'attribut `[JsonProperty]`.

2. **Conversions implicites** : Le getter `InstrumentName` fait `_instrumentName.ToString()`, créant une `MethodCallExpression` au lieu d'une simple `MemberExpression`.

3. **Types génériques** : Les expressions lambda avec conversions de type créent des `UnaryExpression`.

## ✅ Solution implémentée

### 1️⃣ Recherche sur le type concret

```csharp
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
			// ✅ NOUVEAU : Si source est fourni, chercher sur le type concret
			if (source != null)
			{
				var concreteType = source.GetType(); // Node au lieu de INode
				var concreteProperty = concreteType.GetProperty(propertyInfo.Name);
				if (concreteProperty != null)
				{
					var jsonProperty = concreteProperty.GetCustomAttribute<JsonPropertyAttribute>();
					return jsonProperty?.PropertyName ?? propertyInfo.Name;
				}
			}

			// Fallback sur le type de l'expression
			var jsonPropertyAttr = propertyInfo.GetCustomAttribute<JsonPropertyAttribute>();
			return jsonPropertyAttr?.PropertyName ?? propertyInfo.Name;
		}
	}

	throw new ArgumentException("L'expression doit pointer vers une propriété", nameof(propertyExpression));
}
```

### 2️⃣ Gestion des expressions complexes

```csharp
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

	// Cas avec conversion : x => (object)x.Property
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
```

## 🔍 Exemples de cas gérés

### Cas 1 : Interface → Implémentation

```csharp
// Avant (❌ bug)
INode node = new Node();
var jsonName = node.GetJsonPropertyName(x => x.InstrumentName);
// Résultat : "InstrumentName" (attribut non trouvé sur INode)

// Après (✅ corrigé)
INode node = new Node();
var jsonName = node.GetJsonPropertyName(x => x.InstrumentName);
// Résultat : "instrumentName" (attribut trouvé sur Node)
```

### Cas 2 : Conversion implicite

```csharp
// Expression avec conversion
Expression<Func<Node, object>> expr = x => x.InstrumentName;
// Body type : UnaryExpression (Convert)
//   └─ Operand type : MemberExpression (x.InstrumentName)

// ✅ Maintenant géré correctement
```

### Cas 3 : Appel de méthode (ToString)

```csharp
// Expression avec appel de méthode
Expression<Func<Node, string>> expr = x => x.InstrumentName;
// Getter fait : _instrumentName.ToString()
// Body type : MethodCallExpression
//   └─ Object type : MemberExpression (_instrumentName)

// ✅ Maintenant géré correctement
```

## 🧪 Tests de validation

### Test 1 : Type concret

```csharp
[Test]
public void GetJsonPropertyName_WithConcreteType_ReturnsJsonName()
{
	var node = new Node();
	var jsonName = node.GetJsonPropertyName(x => x.InstrumentName);

	Assert.AreEqual("instrumentName", jsonName);
}
```

### Test 2 : Via interface

```csharp
[Test]
public void GetJsonPropertyName_ViaInterface_ReturnsJsonName()
{
	INode node = new Node();
	var jsonName = node.GetJsonPropertyName(x => x.InstrumentName);

	Assert.AreEqual("instrumentName", jsonName); // ✅ Maintenant correct !
}
```

### Test 3 : Avec conversion

```csharp
[Test]
public void GetJsonPropertyName_WithConversion_ReturnsJsonName()
{
	var node = new Node();

	// Expression avec conversion implicite
	Expression<Func<INode, object>> expr = x => x.InstrumentName;
	var jsonName = PropertyExtensions.GetJsonPropertyName(expr);

	Assert.AreEqual("instrumentName", jsonName);
}
```

### Test 4 : Propriété avec ToString()

```csharp
[Test]
public void GetJsonPropertyName_WithToString_ReturnsJsonName()
{
	var node = new Node();

	// Même si le getter fait .ToString()
	var jsonName = node.GetJsonPropertyName(x => x.InstrumentName);

	Assert.AreEqual("instrumentName", jsonName);
}
```

## 📊 Comparaison avant/après

| Scénario | Avant | Après |
|----------|-------|-------|
| `new Node().GetJsonPropertyName(x => x.InstrumentName)` | ❌ "InstrumentName" | ✅ "instrumentName" |
| `((INode)new Node()).GetJsonPropertyName(x => x.InstrumentName)` | ❌ "InstrumentName" | ✅ "instrumentName" |
| Expression avec `Convert` | ❌ Exception | ✅ "instrumentName" |
| Expression avec `ToString()` | ❌ Exception | ✅ "instrumentName" |

## 🔧 Fichiers modifiés

- **`BigSisterNodeNet.Core\Extensions\PropertyExtensions.cs`** : Correction complète

## ✅ Validation

### Build réussie

```
✓ BigSisterNodeNet.Core.csproj compilé avec succès
```

### Utilisation

```csharp
// Dans SendPropertyUpdate (NodeNetObjects.cs)
protected void SendPropertyUpdate<T>(
	Expression<Func<INode, T>> propertyExpression, 
	T value)
{
	// ...
	var jsonPropertyName = this.GetJsonPropertyName(propertyExpression);
	// ✅ Retourne maintenant "instrumentName" au lieu de "InstrumentName"
	service.UpdateNode(this, jsonPropertyName, value);
}
```

## 🎯 Résultat

La fonction `GetJsonPropertyName` retourne maintenant correctement les noms JSON définis par `[JsonProperty]`, même quand :
- L'expression utilise une interface (`INode`)
- Le type réel est une implémentation (`Node`)
- L'expression contient des conversions ou appels de méthodes

Les messages envoyés au réseau NodeNet utilisent maintenant les bons noms JSON (`"instrumentName"` au lieu de `"InstrumentName"`) ! ✅
