# Prompt Copilot - Driver/Editeur WPF pour node `NODENET_SOC`

Tu m'assistes pour developper un driver/editeur Windows sous Visual Studio 2026 en C# / .NET Framework 4.8 / WPF.

Je veux que tu comprennes le fonctionnement du type de noeud NodeNet `NODENET_SOC` et que tu m'aides a implementer une interface CRUD pour configurer ses features et ses points.

## 1. Contexte fonctionnel

- Dans le firmware embarque, `HardwareType::NODENET_SOC = 6`.
- Un `NODENET_SOC` est un noeud NodeNet qui expose un catalogue de points PLC consultable et modifiable par messages JSON NodeNet.
- Le poste Windows n'est pas directement sur le bus RS485 NodeNet.
- Le driver desktop utilise une adresse NodeNet speciale `255`.
- Le PC communique avec un noeud `master` via USB/COM.
- Le `master` route les messages recus sur le port COM vers NodeNet RS485.
- Le `master` route aussi vers le port COM les messages NodeNet adresses a `255` ou en broadcast.
- Le logiciel WPF doit donc se comporter comme un participant logique NodeNet d'adresse `255`, transporte sur lien serie via le master.

## 2. Ce qu'est un `NODENET_SOC`

Le firmware du `NODENET_SOC` expose notamment :

- un `deviceId`
- un `instrumentName`
- un bool `master`
- un `hardwareType = 6`
- un objet `features`
- un catalogue de points PLC

Structure utile cote firmware :

```cpp
enum class HardwareType : uint8_t
{
    FILTER = 0,
    FOCUSER = 1,
    ROTATOR = 2,
    IO8 = 3,
    IO16 = 4,
    ASCOMBRIDGE = 5,
    NODENET_SOC = 6,
    UNDEFINED = 255
};
```

Le header de noeud renvoye par le firmware contient :

```json
{
  "from": 4,
  "deviceId": "gb9fao5yk4f",
  "instrumentName": "NodeNet SoC",
  "master": true,
  "hardwareType": 6
}
```

Le firmware expose aussi un objet `features`, par exemple :

```json
{
  "features": {
    "hasModbus0": true,
    "hasModbus1": false
  }
}
```

## 3. Messages NodeNet a connaitre

Les commandes utiles pour l'editeur CRUD sont :

- `WhoIs` / `IAm`
- `FeaturesReq` / `FeaturesRes`
- `pointDefsReq` / `pointDefsRes`
- `pointStatesReq` / `pointStatesRes`
- `pointUpsert`
- `pointDelete`
- `updateProperty`

Les noms de commandes sont sensibles a la casse et doivent etre utilises tels quels.

## 4. Modele de routage a respecter

Hypothese de transport cote desktop :

- le logiciel ouvre un `SerialPort`
- il envoie des trames JSON vers le `master`
- chaque message doit contenir `from = 255`
- le champ `to` cible l'adresse NodeNet du noeud distant a configurer
- les reponses du noeud cible reviennent vers le PC via le `master`
- les broadcasts NodeNet peuvent aussi remonter vers le PC

Je veux que le code soit structure pour separer clairement :

- le transport serie
- l'encapsulation protocolaire NodeNet JSON
- les services applicatifs de discovery / features / points
- les ViewModels WPF

## 5. Modele de point a implementer cote C#

Le firmware manipule des `PointDefinition` et `PointState`.

### 5.1 PointDefinition

```cpp
struct PointIdentity {
    char device_id[16];
    char feature[32];
    char point_id[32];
};

struct PollingSettings {
    uint32_t refresh_ms = 1000;
    uint32_t timeout_ms = 3000;
};

struct ModbusPointRef {
    uint8_t port_index = 0;
    uint8_t slave_address = 1;
    uint16_t address = 0;
    uint8_t register_count = 1;
    ModbusTable table = HoldingRegisters;
    ModbusAccess access = Read;
};

struct NodeNetPointRef {
    char remote_device_id[16];
    char remote_feature[32];
    char remote_point_id[32];
};

struct PointDefinition {
    PointIdentity id;
    char display_name[32];
    PointBackend backend;
    PointDirection direction;
    PointValueType value_type;
    PollingSettings polling;
    uint16_t string_capacity;
    float scale;
    char unit[10];
    PointBackendRef ref;
};
```

### 5.2 Enums numeriques a respecter

`backend` :

- `0` = `Local`
- `1` = `Modbus`
- `2` = `NodeNet`

`direction` :

- `0` = `Input`
- `1` = `Output`
- `2` = `InOut`

`valueType` :

- `0` = `Bool`
- `1` = `Uint16`
- `2` = `Int16`
- `3` = `Uint32`
- `4` = `Int32`
- `5` = `Float`
- `6` = `Enum`
- `7` = `String`

`table` pour Modbus :

- `1` = `Coils`
- `2` = `DiscreteInputs`
- `3` = `HoldingRegisters`
- `4` = `InputRegisters`

`access` pour Modbus :

- `1` = `Read`
- `2` = `Write`
- `3` = `ReadWrite`

### 5.3 PointState

Le runtime renvoie des etats de points avec :

- `deviceId`
- `feature`
- `pointId`
- `quality`
- `lastUpdateAgeMs`
- `lastGoodUpdateAgeMs`
- `value`

Attention :

- `lastUpdateAgeMs` = age en millisecondes depuis la derniere tentative de lecture/ecriture
- `lastGoodUpdateAgeMs` = age en millisecondes depuis la derniere lecture/ecriture reussie
- `lastGoodUpdateAgeMs = 0` peut vouloir dire qu'aucune bonne valeur n'a encore ete obtenue

Qualites numeriques :

- `0` = `Unknown`
- `1` = `Good`
- `2` = `UncertainInitialValue`
- `3` = `BadNotConnected`
- `4` = `BadNodeMissing`
- `5` = `BadTimeout`
- `6` = `BadProtocolError`
- `7` = `BadConfigError`
- `8` = `BadInvalidValue`
- `9` = `BadWriteRejected`

## 6. Paths hierarchiques

Les points sont identifies par :

- `deviceId`
- `feature`
- `pointId`

Le `path` utilise par les requetes de browse est :

```text
deviceId.feature.pointId
```

Exemples :

- `""` -> browse des devices
- `gb9fao5yk4f` -> browse d'un device
- `gb9fao5yk4f.modbus0.eurotherm6100` -> browse des points d'une feature
- `gb9fao5yk4f.modbus0.eurotherm6100.ch1` -> point exact

## 7. CRUD a implementer dans l'editeur

### 7.1 Lire la structure du noeud

1. Decouvrir le noeud ou se connecter a une adresse connue.
2. Recuperer son identite (`IAm`, `FeaturesRes`, ou autre message d'entete disponible).
3. Afficher `deviceId`, `instrumentName`, `hardwareType`, `master`, `features`.
4. Charger l'arborescence des points avec `pointDefsReq`.
5. Charger les etats runtime avec `pointStatesReq`.

### 7.2 Creer ou modifier un point

Utiliser `pointUpsert` avec un objet `definition` complet.

Exemple :

```json
{
  "cmd": "pointUpsert",
  "from": 255,
  "to": 4,
  "definition": {
    "deviceId": "gb9fao5yk4f",
    "feature": "modbus0.waveshare8ch",
    "pointId": "output1",
    "displayName": "Output Channel 1",
    "backend": 1,
    "direction": 2,
    "valueType": 0,
    "refreshMs": 1000,
    "timeoutMs": 3000,
    "stringCapacity": 0,
    "scale": 1.0,
    "unit": "",
    "portIndex": 0,
    "slaveAddress": 1,
    "address": 0,
    "registerCount": 1,
    "table": 1,
    "access": 3
  }
}
```

### 7.3 Supprimer un point

Utiliser `pointDelete` avec l'identite explicite du point.

```json
{
  "cmd": "pointDelete",
  "from": 255,
  "to": 4,
  "deviceId": "gb9fao5yk4f",
  "feature": "modbus0.waveshare8ch",
  "pointId": "output8"
}
```

### 7.4 Lire les valeurs runtime

Utiliser `pointStatesReq`.

```json
{
  "cmd": "pointStatesReq",
  "from": 255,
  "to": 4,
  "path": "gb9fao5yk4f.modbus0.eurotherm6100",
  "offset": 0,
  "limit": 8
}
```

### 7.5 Ecrire certaines valeurs runtime

Certaines ecritures se font via `updateProperty`, pas via `pointUpsert`.

Deux categories existent deja :

- proprietes locales du `NodeNetCore`, par exemple `instrumentName`, `master`, `modbus0.speed`, `modbus0.timeout`, `modbus0.retries`, `modbus0.interframeCharsQ1`
- points Modbus coil ecrits via leur path complet

Exemple :

```json
{
  "cmd": "updateProperty",
  "from": 255,
  "to": 4,
  "propertyName": "gb9fao5yk4f.modbus0.waveshare8ch.output1",
  "value": true
}
```

## 8. Contraintes UI / UX attendues

Je veux une interface WPF de type MVVM avec :

- une zone de connexion serie : port COM, baudrate, connect/disconnect
- une zone de selection du noeud cible NodeNet : adresse, `deviceId`, `instrumentName`
- une vue arborescente des features et points
- une grille des definitions de points
- une grille des etats runtime
- une boite de dialogue Create/Edit Point
- des commandes `Refresh`, `Create`, `Edit`, `Delete`, `Read States`, `Write Property`
- des indicateurs d'etat : connecte, timeout, dernier message, erreurs protocole

Je veux que les operations soient asynchrones et qu'elles n'interrompent pas l'UI.

## 9. Architecture C# demandee

Je veux que tu proposes et implementes des classes proches de celles-ci :

- `SerialNodeNetTransport`
- `NodeNetMessageEnvelope`
- `NodeNetSocClient`
- `NodeDiscoveryService`
- `PointCatalogService`
- `PointDefinitionDto`
- `PointStateDto`
- `NodeInfoDto`
- `MainViewModel`
- `PointEditorViewModel`

Recommandations :

- utiliser `INotifyPropertyChanged`
- utiliser `ObservableCollection<T>` pour les listes affichees
- utiliser `async/await`
- encapsuler les timeouts et correler requete/reponse quand c'est possible
- journaliser les JSON TX/RX bruts pour debug
- separer DTO reseau, modeles UI et services

## 10. Validations de formulaire a implementer

Pour l'editeur de point, ajoute des validations simples mais strictes :

- `deviceId`, `feature`, `pointId`, `displayName` obligatoires
- si `backend = Modbus`, exiger `portIndex`, `slaveAddress`, `address`, `registerCount`, `table`, `access`
- si `backend = NodeNet`, exiger `remoteDeviceId`, `remoteFeature`, `remotePointId`
- si `valueType = String`, gerer `stringCapacity`
- `unit` longueur max 10 caracteres
- `deviceId` longueur max 15 caracteres utiles
- `feature` longueur max 31 caracteres utiles
- `pointId` longueur max 31 caracteres utiles
- `displayName` longueur max 31 caracteres utiles

## 11. Pagination a gerer

Les commandes `pointDefsReq` et `pointStatesReq` sont paginees.

Le code client doit donc savoir :

- envoyer `offset` et `limit`
- lire `count`, `total`, `hasMore`
- boucler tant que `hasMore = true`
- reconstruire une liste complete cote client

## 12. Ce que je veux que tu fasses

Quand je te demanderai du code, je veux que tu :

1. proposes une structure de projet WPF claire
2. crees les DTO JSON necessaires
3. implementes le service de transport serie
4. implementes le client NodeNet pour `pointDefsReq`, `pointStatesReq`, `pointUpsert`, `pointDelete`, `updateProperty`
5. implementes les ViewModels MVVM
6. implementes les vues WPF XAML minimales mais propres
7. gardes le code testable et decouple

## 13. Points d'attention importants

- Ne reinvente pas le protocole : utilise exactement les noms de commandes JSON ci-dessus.
- Le noeud desktop local a l'adresse logique `255`.
- Le noeud distant a configurer est typiquement un `NODENET_SOC` avec `hardwareType = 6`.
- Le master serie/USB agit comme bridge entre COM et RS485 NodeNet.
- `pointUpsert` modifie la definition d'un point, pas sa valeur runtime.
- `updateProperty` sert aux ecritures runtime prises en charge.
- `pointStatesRes` renvoie des ages (`lastUpdateAgeMs`, `lastGoodUpdateAgeMs`), pas des timestamps absolus.
- La compatibilite cible est .NET Framework 4.8, donc eviter les APIs reservees a .NET 6+.

## 14. Premiere tache que je te demanderai

Quand je te dirai de commencer, je veux que tu me proposes d'abord :

- l'arborescence de projet
- les DTO C# pour tous les messages JSON utiles
- le service `SerialNodeNetTransport`
- le service `NodeNetSocClient`
- un `MainViewModel` minimal
- une fenetre WPF minimale capable de : connecter le COM, envoyer `pointDefsReq`, afficher la liste des points
