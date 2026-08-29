# Extension GetJsonPropertyName

## Vue d'ensemble

L'extension `GetJsonPropertyName` permet de récupérer automatiquement le nom de propriété JSON (défini par l'attribut `[JsonProperty]`) au lieu du nom de propriété C#. Cela garantit que le nom envoyé au réseau NodeNet correspond exactement au format JSON attendu.

## Problème résolu

**Avant** :
```csharp
[JsonProperty("instrumentName")]
public string InstrumentName { 
	get => _instrumentName; 
	set {
		if(SetProperty(ref _instrumentName, value))
		{
			// ❌ Envoie "InstrumentName" au lieu de "instrumentName"
			_nodeUpdateService.UpdateNode(this, nameof(InstrumentName), value);
		}
	}
}
```

**Après** :
```csharp
[JsonProperty("instrumentName")]
public string InstrumentName { 
	get => _instrumentName; 
	set {
		if(SetProperty(ref _instrumentName, value))
		{
			// ✅ Envoie "instrumentName" (le nom JSON correct)
			_nodeUpdateService.UpdateNode(this, this.GetJsonPropertyName(x => x.InstrumentName), value);
		}
	}
}
```

## Utilisation

### Méthode d'instance

```csharp
// Dans une classe avec des propriétés annotées JsonProperty
var jsonName = this.GetJsonPropertyName(x => x.InstrumentName);
// Retourne: "instrumentName"
```

### Méthode statique

```csharp
// Depuis n'importe où
var jsonName = PropertyExtensions.GetJsonPropertyName<Node, string>(x => x.InstrumentName);
// Retourne: "instrumentName"
```

## Exemples complets

### Exemple 1 : Dans un setter de propriété

```csharp
public class Node : INode
{
	private string _instrumentName;
	[JsonProperty("instrumentName")]
	public string InstrumentName 
	{ 
		get => _instrumentName; 
		set 
		{ 
			if(SetProperty(ref _instrumentName, value))
			{
				var jsonPropertyName = this.GetJsonPropertyName(x => x.InstrumentName);
				_nodeUpdateService.UpdateNode(this, jsonPropertyName, value);
			}
		} 
	}
}
```

### Exemple 2 : Dans une méthode de commande

```csharp
public void UpdateProperty<T>(Expression<Func<Node, T>> propertyExpression, T value)
{
	var jsonPropertyName = this.GetJsonPropertyName(propertyExpression);
	_nodeUpdateService.UpdateNode(this, jsonPropertyName, value);
}

// Utilisation
UpdateProperty(x => x.Temperature, 25.5f);
// Envoie "temperature" (pas "Temperature")
```

### Exemple 3 : Propriété sans JsonProperty

```csharp
public class Node
{
	// Pas d'attribut JsonProperty
	public bool IsOnline { get; set; }
}

var jsonName = node.GetJsonPropertyName(x => x.IsOnline);
// Retourne: "IsOnline" (fallback sur le nom de propriété C#)
```

## Avantages

1. ✅ **Type-safe** : Utilise des expressions lambda, donc détection d'erreurs à la compilation
2. ✅ **Refactoring-safe** : Si vous renommez une propriété, le code reste fonctionnel
3. ✅ **Maintenable** : Un seul endroit (JsonProperty) définit le nom JSON
4. ✅ **Fallback** : Si JsonProperty n'existe pas, retourne le nom de la propriété C#
5. ✅ **IntelliSense** : Auto-complétion complète dans Visual Studio

## Comparaison avec les alternatives

| Approche | Type-safe | Refactoring | Correct |
|----------|-----------|-------------|---------|
| `nameof(InstrumentName)` | ✅ | ✅ | ❌ (retourne "InstrumentName") |
| `"instrumentName"` | ❌ | ❌ | ✅ |
| `this.GetJsonPropertyName(x => x.InstrumentName)` | ✅ | ✅ | ✅ |

## Performance

L'extension utilise la réflexion pour lire l'attribut `JsonProperty`. Si vous appelez cette méthode fréquemment, vous pouvez mettre en cache le résultat :

```csharp
public class Node
{
	private static readonly string InstrumentNameJsonProperty = 
		PropertyExtensions.GetJsonPropertyName<Node, string>(x => x.InstrumentName);

	private string _instrumentName;
	[JsonProperty("instrumentName")]
	public string InstrumentName 
	{ 
		get => _instrumentName; 
		set 
		{ 
			if(SetProperty(ref _instrumentName, value))
			{
				// Utilise la valeur mise en cache
				_nodeUpdateService.UpdateNode(this, InstrumentNameJsonProperty, value);
			}
		} 
	}
}
```

## Tests unitaires

```csharp
[Test]
public void GetJsonPropertyName_WithJsonPropertyAttribute_ReturnsJsonName()
{
	var node = new Node();
	var jsonName = node.GetJsonPropertyName(x => x.InstrumentName);

	Assert.AreEqual("instrumentName", jsonName);
}

[Test]
public void GetJsonPropertyName_WithoutJsonPropertyAttribute_ReturnsPropertyName()
{
	var node = new Node();
	var jsonName = node.GetJsonPropertyName(x => x.IsOnline);

	Assert.AreEqual("IsOnline", jsonName);
}

[Test]
public void GetJsonPropertyName_StaticVersion_ReturnsJsonName()
{
	var jsonName = PropertyExtensions.GetJsonPropertyName<Node, string>(x => x.InstrumentName);

	Assert.AreEqual("instrumentName", jsonName);
}
```

## Implémentation

Voir `BigSisterNodeNet.Core\Extensions\PropertyExtensions.cs` pour l'implémentation complète.
