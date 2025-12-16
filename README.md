# Système de Gestion de Code d'Accès - Projet socket RC52

**Auteurs :** Corentin DEFIOLLES & Arona NGOM  
**Jalon 4** - API Socket avec authentification sécurisée

---

## Description

Application client-serveur en C permettant la gestion d'un système de verrouillage avec codes d'accès. 
Le serveur utilise SQLite pour stocker l'historique des tentatives et les comptes utilisateurs avec mots de passe hashés en bcrypt.
Les jalons 1 2 3 étant déjà montré au prof, ce readme va faire le tour de tout ce qu'on fait jusque là principalement les ajouts apportés dans le jalon 4.
Le système distingue deux types d'utilisateurs :

- **OWNER** : Propriétaire qui peut définir et gérer les codes d'accès
- **TENANT** : Locataire qui peut tenter d'entrer des codes pour obtenir l'accès

---

## Architecture

### Composants

- **`server.c`** : Serveur TCP/IP gérant les connexions multiples via `poll()`
- **`client.c`** : Client interactif avec interface en ligne de commande
- **`history.db`** : Base de données SQLite (créée automatiquement)

### Technologies

- **Sockets TCP/IP** : Communication réseau
- **SQLite3** : Base de données pour historique et utilisateurs
- **bcrypt** : Hachage sécurisé des mots de passe
- **poll()** : Multiplexage I/O pour gérer plusieurs clients

---

## Fonctionnement

### Côté Serveur

1. **Initialisation**
   - Création du socket d'écoute sur le port spécifié
   - Initialisation de la base de données SQLite (`history.db`)
   - Création des tables `history` et `users` si elles n'existent pas
   - Hachage des mots de passe par défaut avec bcrypt

2. **Boucle principale (poll loop)**
   - Utilise `poll()` pour surveiller le socket d'écoute et tous les clients connectés
   - Accepte les nouvelles connexions
   - Gère les événements de lecture/écriture sur chaque socket client

3. **Gestion des clients**
   - **Phase d'authentification** : Le client doit envoyer `AUTH <ROLE> <pseudo> <password>`
   - **Vérification** : Le serveur compare le mot de passe avec le hash bcrypt stocké
   - **Attribution du rôle** : OWNER ou TENANT selon l'authentification

4. **Fonctionnalités OWNER**
   - `SET CODE <code>` : Définit un nouveau code à 6 chiffres
   - `SET VALIDITY <secondes>` : Modifie la durée de validité du code
   - `SHOW` : Affiche le code actuel et le temps restant
   - `QUIT` : Déconnexion

5. **Fonctionnalités TENANT**
   - Tentative de code : Envoie un code à 6 chiffres
   - **Système d'alarme** : Après 3 tentatives échouées, le code est régénéré et l'OWNER est notifié
   - **Expiration** : Si le code expire, un nouveau est généré automatiquement

6. **Historique**
   - Toutes les tentatives sont enregistrées dans `history.db`
   - Types d'événements : `success`, `failed attempt`, `alarm triggered`, `code expired`

### Côté Client

1. **Connexion**
   - Connexion TCP au serveur
   - Envoi automatique des identifiants (`AUTH <ROLE> <pseudo> <password>`)
   - Attente de confirmation d'authentification

2. **Interface interactive**
   - Menu contextuel selon le rôle (OWNER ou TENANT)
   - Utilise `poll()` pour gérer simultanément l'entrée clavier et les messages serveur
   - Affichage en temps réel des réponses du serveur

3. **Commandes simplifiées**
   - **OWNER** : `1 <code>`, `2 <sec>`, `3`, `4` (QUIT)
   - **TENANT** : `1 <code>`, `2` (QUIT)

---

## Requirements

### Système

- **OS** : Linux (Debian/Ubuntu/WSL)
- **Compilateur** : GCC
- **Bibliothèques** :
  - `libsqlite3-dev` : Headers SQLite3
  - `libcrypt-dev` : Bibliothèque crypt pour bcrypt 

### Paquets nécessaires

#### Debian / Ubuntu / WSL

```bash
sudo apt update
sudo apt install -y build-essential libsqlite3-dev
```

---

## Installation et Compilation

### Compilation

```bash
# Compiler le serveur
gcc server.c -o server -lsqlite3 -lcrypt

# Compiler le client
gcc client.c -o client
```

### Vérification

```bash
# Vérifier que les exécutables sont créés
ls -lh server client
```

---

## Utilisation

### 1. Démarrer le serveur

Dans un premier terminal :

```bash
./server 8000
```

**Sortie attendue :**
```
Socket created
bind done
Waiting for incoming connections...
```

Le serveur crée automatiquement `history.db` s'il n'existe pas.

### 2. Connecter un client OWNER

Dans un deuxième terminal :

```bash
./client 127.0.0.1 8000 OWNER Arona aronapass
```

**Sortie attendue :**
```
Réponse du serveur : "WELCOME Arona CODE 123456 VALIDITY 3600"
Connecté (auth OK)

=== Menu OWNER ===
1 <code> : SET CODE <code> (6 chiffres)
2 <sec>  : SET VALIDITY <sec>
3        : SHOW (code + durée restante)
4        : QUIT
-------------------------------
```

### 3. Connecter un client TENANT

Dans un troisième terminal :

```bash
./client 127.0.0.1 8000 TENANT Corentin corentinpass
```

**Sortie attendue :**
```
Réponse du serveur : "CURRENT CODE 123456 VALIDITY 3600
ENTER CODE"
Connecté (auth OK)

=== Menu TENANT ===
1 <code> : tenter un code (6 chiffres)
2        : QUIT
-------------------------------
```

---

## Exemple de Session réalisée en classe pour notre démo

### Terminal 1 - Serveur
```bash
$ ./server 8000
Socket created
bind done
Waiting for incoming connections...
New client 127.0.0.1:54321
Received from client fd=4 [127.0.0.1:54321]: AUTH OWNER Arona aronapass
New client 127.0.0.1:54322
Received from client fd=5 [127.0.0.1:54322]: AUTH TENANT Corentin corentinpass
Received from client fd=5 [127.0.0.1:54322]: 123456
```

### Terminal 2 - Client OWNER
```bash
$ ./client 127.0.0.1 8000 OWNER Arona aronapass
Réponse du serveur : "WELCOME Arona CODE 789012 VALIDITY 3600"
Connecté (auth OK)

=== Menu OWNER ===
1 <code> : SET CODE <code> (6 chiffres)
2 <sec>  : SET VALIDITY <sec>
3        : SHOW (code + durée restante)
4        : QUIT
-------------------------------
3
Réponse du serveur : "OK CODE 789012 VALIDITY 3598"
1 123456
Réponse du serveur : "OK CODE 123456 VALIDITY 3600"
```

### Terminal 3 - Client TENANT
```bash
$ ./client 127.0.0.1 8000 TENANT Corentin corentinpass
Réponse du serveur : "CURRENT CODE 123456 VALIDITY 3600
ENTER CODE"
Connecté (auth OK)

=== Menu TENANT ===
1 <code> : tenter un code (6 chiffres)
2        : QUIT
-------------------------------
1 123456
Réponse du serveur : "ACCESS GRANTED"
1 000000
Réponse du serveur : "INVALID CODE (1/3)"
1 111111
Réponse du serveur : "INVALID CODE (2/3)"
1 222222
Réponse du serveur : "ALARM TRIGGERED"
```

---

## Comptes par Défaut

Les comptes suivants sont créés automatiquement dans `history.db` :

| Pseudo   | Rôle    | Mot de passe |
|----------|---------|--------------|
| owner    | OWNER   | ownerpass    |
| Arona    | OWNER   | aronapass    |
| tenant   | TENANT  | tenantpass   |
| Corentin | TENANT  | corentinpass  |

> **Sécurité** : Tous les mots de passe sont hashés avec bcrypt dans la base de données.

---

## Base de Données

### Structure

La base de données `history.db` contient deux tables :

#### Table `users`
```sql
CREATE TABLE users (
    pseudo TEXT PRIMARY KEY,
    role TEXT NOT NULL CHECK(role IN ('OWNER','TENANT')),
    password TEXT NOT NULL  -- Hash bcrypt (60 caractères)
);
```

#### Table `history`
```sql
CREATE TABLE history (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    ts INTEGER NOT NULL,           -- Timestamp Unix
    pseudo TEXT NOT NULL,
    result TEXT NOT NULL            -- 'success', 'failed attempt', 'alarm triggered', 'code expired'
);
```

### Consultation

```bash
# Installer sqlite3 si nécessaire
sudo apt install sqlite3

# Ouvrir la base de données
sqlite3 history.db

# Voir le schéma
.schema

# Consulter l'historique
SELECT id, datetime(ts,'unixepoch','localtime') AS timestamp, 
       pseudo, result 
FROM history 
ORDER BY ts DESC 
LIMIT 20;

# Voir les utilisateurs (sans afficher les mots de passe)
SELECT pseudo, role FROM users;
```

---

## Sécurité

### Mots de passe

- **Hachage** : bcrypt avec facteur de coût 10
- **Salt** : Généré automatiquement et de manière sécurisée
- **Stockage** : Seuls les hash sont stockés dans la base de données

### Gestion des erreurs réseau

- **Envoi complet** : Utilisation de `send_all()` pour garantir l'envoi complet des messages
- **Réception** : Détection des messages tronqués
- **Gestion mémoire** : Tous les `malloc`/`calloc` sont correctement libérés

### Système d'alarme

- **3 tentatives échouées** : Déclenchement de l'alarme
- **Régénération automatique** : Nouveau code généré après alarme ou expiration
- **Notification OWNER** : L'OWNER est immédiatement notifié des événements critiques

---


## Notes Techniques

### Architecture du code

- **Modularité** : Fonctions bien séparées par responsabilité
- **Gestion mémoire** : Tous les `malloc`/`calloc` sont libérés
- **Gestion réseau** : Utilisation de `send_all()` et vérification des réceptions
- **Main() court** : Moins de 50 lignes, logique déléguée aux fonctions

### Limitations

- Messages limités à 1024 caractères (`MSG_LEN`)
- Maximum 16 clients simultanés (`BACKLOG`)
- Codes à 6 chiffres uniquement
- Pas de support TLS/SSL (communication en clair)

---

## Auteurs

- **Corentin DEFIOLLES**
- **Arona NGOM**

Projet réalisé dans le cadre du cours RC52 - API Socket (Jalon 4)

---
