# Utilisation du service NodeUpdate dans NodeViewModel

## Vue d'ensemble

Le service `NodeUpdate` est maintenant accessible dans `NodeViewModel` via la propriété `NodeUpdateService`. Cela permet d'envoyer des commandes de mise à jour aux nœuds du réseau NodeNet.

## Accès au service

```csharp
public class NodeViewModel : ObservableObject
{
	private readonly NodeUpdate _nodeUpdate;

	public NodeViewModel(INode node, NodeUpdate nodeUpdate = null)
	{
		_node = node;
		_nodeUpdate = nodeUpdate;
	}

	/// <summary>
	/// Service pour mettre à jour les propriétés du nœud via le réseau NodeNet
	/// </summary>
	public NodeUpdate NodeUpdateService => _nodeUpdate;
}
```

## Exemples d'utilisation

### Exemple 1 : Mettre à jour une propriété depuis un ViewModel

```csharp
public class FilterNodeViewModel : NodeViewModel
{
	private int _filterPosition;

	public int FilterPosition
	{
		get => _filterPosition;
		set
		{
			if (SetProperty(ref _filterPosition, value))
			{
				// Envoyer la mise à jour au nœud via NodeUpdate
				NodeUpdateService?.UpdateNode(Node, "FilterPosition", value);
			}
		}
	}
}
```

### Exemple 2 : Commande RelayCommand

```csharp
public class NodeViewModel : ObservableObject
{
	public RelayCommand<string> UpdatePropertyCommand { get; }

	public NodeViewModel(INode node, NodeUpdate nodeUpdate = null) : base(node, nodeUpdate)
	{
		UpdatePropertyCommand = new RelayCommand<string>(
			propertyName => 
			{
				if (NodeUpdateService != null)
				{
					// Logique pour obtenir la valeur
					var value = GetPropertyValue(propertyName);
					NodeUpdateService.UpdateNode(Node, propertyName, value);
				}
			},
			_ => NodeUpdateService != null // La commande est activée seulement si le service est disponible
		);
	}
}
```

### Exemple 3 : Vérifier la disponibilité du service

```csharp
public void TryUpdateNode(string propertyName, object value)
{
	if (NodeUpdateService == null)
	{
		// Le service n'est pas disponible (mode tests ou initialisation incomplète)
		MessageBox.Show("Le service de mise à jour n'est pas disponible.");
		return;
	}

	NodeUpdateService.UpdateNode(Node, propertyName, value);
}
```

## Architecture

```
NodeNetCore
	└─ NodeUpdateService (propriété publique)
		   │
		   └─ Injecté dans NodeViewModel
				  │
				  └─ Accessible via NodeUpdateService (propriété)
```

## Configuration de l'injection

Dans `NodeNetCore` :

```csharp
private void ConfigureServices(IServiceCollection services)
{
	// ...
	services.AddSingleton<NodeUpdate>();
}

// Exposition du service
public NodeUpdate NodeUpdateService => _serviceProvider?.GetService<NodeUpdate>();
```

Dans `NodeNetViewModel` :

```csharp
private void NodeUpdated(object sender, INode e)
{
	Application.Current.Dispatcher.Invoke(() =>
	{
		var nodeViewModel = new NodeViewModel(e, _nodeNet.NodeUpdateService);
		Nodes.Add(nodeViewModel);
	});
}
```

## Tests unitaires

Pour les tests, vous pouvez passer `null` ou un mock :

```csharp
// Sans service (pour les tests UI simples)
var viewModel = new NodeViewModel(mockNode, null);

// Avec un mock du service
var mockNodeUpdate = new Mock<NodeUpdate>();
var viewModel = new NodeViewModel(mockNode, mockNodeUpdate.Object);

// Vérifier les appels
viewModel.UpdateProperty("test", 42);
mockNodeUpdate.Verify(s => s.UpdateNode(It.IsAny<INode>(), "test", 42), Times.Once);
```

## Avantages

1. ✅ **Découplage** : Le ViewModel ne dépend pas directement de `BlockingCollection` ou des détails d'implémentation
2. ✅ **Testabilité** : Facile de mocker `NodeUpdate` pour les tests
3. ✅ **Optionnel** : Le paramètre est optionnel (`= null`), permettant la rétrocompatibilité
4. ✅ **Clair** : L'intention est claire via la propriété `NodeUpdateService`
