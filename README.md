# LED_Crowns

Projet PlatformIO / Arduino pour les couronnes LED de la performance
"Shaggirrra & The Queens", prevue au festival "Chauffer dans la noirceur"
le 10 juillet 2026.

Ce fichier sert aussi de memoire de projet pour les prochaines sessions avec
Codex.

## Intention artistique

Le projet accompagne un DJ set de Shaggirrra avec plusieurs danseuses amateures,
les Queens. La performance melange theatre sans parole, expression libre et
danse voguing.

Les danseuses representent des femmes prisonnieres d'injonctions. Elles s'en
liberent progressivement et deviennent des Queens. Ce passage est symbolise par
un couronnement : les couronnes s'illuminent progressivement, puis leurs effets
lumineux se synchronisent pour representer la sororite entre les Queens.

## Materiel

### Master

- ESP32-WROOM-32.
- Utilise branche a l'ordinateur pour le flash, l'initialisation des tags et
  les logs de test.
- Utilise autonome pendant le show, porte par Yara sur son sceptre.
- Declenche les activations et effets envoyes aux couronnes.
- Lecteur RFID MFRC522 branche sur le master.
- Tags RFID autocollants associes aux couronnes.

### Remote regie

- ESP32-C3 Super Mini.
- Branche en USB-C a un telephone Android.
- Utilise avec l'application "Serial USB Terminal".
- Recoit des commandes texte via Serial USB.
- Envoie ces commandes au master WROOM en ESP-NOW.
- Sert de canal de secours : le master reste autonome avec le RFID si aucune
  commande regie n'est recue.

### Couronnes

Chaque couronne contient :

- ESP32-C3 Super Mini.
- Batterie LiPo 1000 mAh.
- Module de charge TP4056.
- Convertisseur boost MT3608.
- Ruban de 32 LEDs WS2812B.
- Condensateur 1000 uF entre VIN et GND juste avant le ruban LED.

Les couronnes sont imprimees en 3D.

## Architecture actuelle

Le systeme est entierement autonome et ne depend pas du Wi-Fi du lieu. La
communication utilise ESP-NOW en broadcast.

Il y a deux environnements PlatformIO :

- `crown_c3` : firmware des couronnes ESP32-C3.
- `master_wroom` : firmware du master ESP32-WROOM-32.
- `remote_c3` : firmware du C3 branche au telephone Android pour la regie.

Fichiers principaux :

- `platformio.ini` : configuration PlatformIO.
- `include/config.h` : constantes radio, hardware, protocole et structure de
  message commune.
- `src/master_main.cpp` : master RFID, mapping `tag -> couronne -> effet`, logs Serial.
- `src/node_main.cpp` : reception ESP-NOW, anti-doublon, activation ciblee,
  synchronisation differee et relais non bloquant.
- `src/remote_main.cpp` : telecommande regie Serial USB vers ESP-NOW.
- `include/effects.h` / `src/effects.cpp` : moteur FastLED et bibliotheque
  d'effets.
- `src/mac_addresses.txt` : adresses MAC connues des couronnes et du master.

## Reglages critiques a conserver

Les premiers tests ont montre que les antennes des ESP32-C3 Super Mini peuvent
etre capricieuses. Les reglages radio actuels donnent une bonne portee et une
reception satisfaisante.

Ces valeurs doivent etre modifiees avec prudence :

- `WIFI_CHANNEL = 1`
- `TX_POWER_LEVEL = 15`
- `MSG_REDUNDANCY_COUNT = 3`
- `MSG_REDUNDANCY_DELAY = 4`
- `SIGNAL_TIMEOUT = 2600`
- `MAX_HOPS = 6`

Le systeme de redondance est important pour limiter les pertes. Les
retransmissions doivent rester non bloquantes.

## Contraintes importantes

Eviter `delay()` autant que possible, surtout cote couronnes. Les tests ont
montre que pendant un `delay`, un ESP32-C3 peut rater des receptions ESP-NOW.

La boucle des couronnes doit rester reactive :

- reception ESP-NOW courte ;
- relais programme sans blocage ;
- effets FastLED mis a jour avec `millis()`.

La limitation de puissance FastLED est conservee :

- `LED_POWER_VOLTAGE = 5`
- `LED_POWER_MILLIAMPS = 400`

Cette limite logicielle aide a proteger la batterie, le MT3608 et le ruban LED,
mais ne remplace pas une protection electrique materielle.

## Logique de show actuelle

Au demarrage, les couronnes restent eteintes.

Le master est prevu pour etre porte par Yara dans un boitier fixe sur son
sceptre, avec le lecteur RFID et une alimentation autonome. Il n'a donc pas
besoin d'etre connecte a l'ordinateur pendant le show.

Yara couronne les Queens en scannant le tag NFC associe a chaque couronne :

1. le master reconnait le tag ;
2. il envoie une commande `COMMAND_ACTIVATE_CROWN` en broadcast, avec l'adresse
   MAC de la couronne cible ;
3. seule la couronne cible s'active immediatement et joue une sequence de
   couronnement techno/electro ;
4. a la fin de cette sequence, la couronne cible joue son effet associe ;
5. les couronnes deja activees attendent `CROWN_SYNC_DELAY` puis se
   synchronisent sur l'effet de la couronne qui vient d'etre activee ;
6. les couronnes pas encore activees restent eteintes.

La synchronisation differee est actuellement construite avec ces timings :

- `ACTIVATION_SPARKLE_DURATION = 5000`
- `ACTIVATION_STROBE_DURATION = 420`
- `GROUP_SYNC_WAIT_AFTER_ACTIVATION = 2000`
- `SYNC_STROBE_DURATION = 420`
- `CROWN_SYNC_DELAY = ACTIVATION_SEQUENCE_DURATION + GROUP_SYNC_WAIT_AFTER_ACTIVATION`

Choregraphie actuelle d'un couronnement :

1. la nouvelle couronne joue un sparkle sur fond noir pendant 5 secondes ;
2. les LEDs s'allument aleatoirement et brievement, comme des flashs photo ;
3. la couleur progresse violet -> bleu fonce -> bleu clair -> blanc ;
4. la frequence des flashs et le nombre de LEDs pouvant flasher augmentent ;
5. la nouvelle couronne et les couronnes deja actives font 3 flashs blancs
   rapides en meme temps ;
6. les couronnes deja actives s'eteignent ;
7. la nouvelle couronne prend son effet avec une montee de luminosite sur 2
   secondes ;
8. au bout de ces 2 secondes, les couronnes deja actives refont 3 flashs blancs ;
9. elles prennent ensuite le nouvel effet avec la meme montee de luminosite sur
   2 secondes.

Pendant les sequences d'activation et de strobe, les heartbeats d'effet sont
ignores par les couronnes concernees pour eviter les glitches visuels. Les
commandes de blackout et reset restent prioritaires.

Apres chaque activation, le master continue d'envoyer un heartbeat avec l'effet
courant toutes les 2 secondes pour maintenir l'etat radio.

Si une couronne activee perd le signal, elle continue a jouer le dernier effet
connu. Seule la premiere LED passe en rouge fixe tant que le signal n'est pas
retabli. Une couronne jamais activee reste eteinte, meme sans signal.

## Canal regie Android / C3 remote

Un ESP32-C3 Super Mini peut etre branche en USB-C a un telephone Android avec
l'application "Serial USB Terminal". Il lit des commandes texte, puis les envoie
au master WROOM par ESP-NOW.

Le WROOM reste le master scenique principal :

- si aucune commande regie n'est recue, le RFID fonctionne comme avant ;
- si une commande regie arrive, le WROOM la traduit en commande ESP-NOW pour
  les couronnes ;
- les logs Serial du WROOM restent lisibles pour les tests ;
- le C3 remote affiche aussi ce qu'il envoie dans le terminal du telephone.

Commandes serie cote telephone :

| Commande | Action |
| --- | --- |
| `b` ou `blackout` | blackout general, couronnes eteintes et desactivees |
| `r` ou `reset` | reset couronnes, retour eteint |
| `t` ou `test` | test reseau avec `EFFECT_DEBUG_HOPS` |
| `f` ou `final` | effet final global `EFFECT_PRISM` |
| `cA`, `cD`, `cO`, etc. | activation manuelle d'une couronne |
| `h` ou `help` | aide dans le terminal |

L'effet final utilise actuellement `EFFECT_PRISM`, volontairement non associe a
une couronne dans le mapping RFID.

Pour que blackout, reset, test et final aient le comportement complet, flasher
les trois firmwares compatibles :

- `master_wroom`
- `remote_c3`
- `crown_c3`

## Association tag / couronne / effet

Le mapping est dans `src/master_main.cpp`, tableau `crownActivations`.

Chaque entree contient :

- le nom de couronne (`"A"`, `"D"`, etc.) ;
- l'adresse MAC de la couronne ;
- l'UID du tag NFC ;
- la longueur de l'UID ;
- l'effet et ses parametres.

Pour initialiser les tags :

1. flasher le master ;
2. le connecter a l'ordinateur juste pour cette phase d'initialisation ;
3. ouvrir le moniteur serie a 115200 bauds ;
4. scanner un tag NFC ;
5. recopier l'UID affiche dans l'entree `crownActivations` correspondante ;
6. mettre `tagLength` au nombre d'octets de l'UID ;
7. reflasher le master.

Exemple pour un UID affiche `04:A1:B2:C3:D4:E5:F6` :

```cpp
{"A", {0x3C, 0xDC, 0x75, 0x33, 0x78, 0x74},
 {0x04, 0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6}, 7,
 EFFECT_SOLID, "SOLID warm gold", 150, 0, 0xFFD060, 0x000000},
```

Tant que `tagLength = 0`, le tag n'est pas encore associe et ne declenchera
aucune activation.

Associations configurees :

| Couronne | UID NFC | Effet |
| --- | --- | --- |
| O | `04:5F:96:73:CE:2A:81` | `EFFECT_BREATH` |
| F | `04:7D:A3:73:CE:2A:81` | `EFFECT_SPARKLE` |
| Q | `04:91:A9:73:CE:2A:81` | `EFFECT_WAVE` |
| R | `04:75:BA:73:CE:2A:81` | `EFFECT_COLOR_CHASE` |
| D | `04:92:C1:73:CE:2A:81` | `EFFECT_THEATER_CHASE` |
| A | `04:A8:C7:73:CE:2A:81` | `EFFECT_PORTAL` |
| I | `04:D2:CD:73:CE:2A:81` | `EFFECT_GLITTER_RAIN` |
| M | `04:E6:D4:73:CE:2A:81` | `EFFECT_RIPPLE` |
| T | `04:19:E2:73:CE:2A:81` | `EFFECT_CONSTELLATION` |
| L | `04:26:E8:73:CE:2A:81` | `EFFECT_VOGUE_POSE` |
| K | `04:66:9C:73:CE:2A:81` | `EFFECT_STORM` |
| E | `04:7E:A3:73:CE:2A:81` | `EFFECT_QUEEN_AURA` |

Les boitiers `H`, `J`, `N` et `S` restent disponibles pour de futurs objets
lumineux ou besoins de reserve. Ils n'ont pas de tag associe actuellement.

## Protocole de message

La structure commune est `struct_message` dans `include/config.h`.

Champs importants :

- `protocolVersion` : version du protocole.
- `command` : type de message (`COMMAND_HEARTBEAT` ou
  `COMMAND_ACTIVATE_CROWN`).
- `sessionId` : genere par le master au demarrage avec `esp_random()`.
- `msgId` : compteur de message.
- `hopCount` : nombre de relais deja effectues.
- `effectId` : effet a jouer.
- `intensity` : intensite de l'effet.
- `speed` : vitesse de l'effet.
- `primaryColor` / `secondaryColor` : couleurs RGB encodees en `0xRRGGBB`.
- `flags` : reserve pour usage futur.
- `targetMac` : adresse MAC de la couronne cible lors d'une activation.

Le couple `sessionId` + `msgId` permet de filtrer les doublons tout en restant
fonctionnel si le master est reset. Si le master redemarre, il genere un nouveau
`sessionId`, et les couronnes acceptent la nouvelle sequence de messages.

## Effets actuels

Les effets existants sont :

- `EFFECT_OFF`
- `EFFECT_DEBUG_HOPS`
- `EFFECT_SOLID`
- `EFFECT_BREATH`
- `EFFECT_CORONATION`
- `EFFECT_SPARKLE`
- `EFFECT_WAVE`
- `EFFECT_AURORA`
- `EFFECT_COMET_TWINS`
- `EFFECT_HEARTBEAT`
- `EFFECT_COLOR_CHASE`
- `EFFECT_THEATER_CHASE`
- `EFFECT_PORTAL`
- `EFFECT_FIREWORKS`
- `EFFECT_GLITTER_RAIN`
- `EFFECT_LARSON_SCANNER`
- `EFFECT_PRISM`
- `EFFECT_RIPPLE`
- `EFFECT_CONSTELLATION`
- `EFFECT_VOGUE_POSE`
- `EFFECT_STORM`
- `EFFECT_QUEEN_AURA`

Le master ne parcourt plus automatiquement une playlist de demo dans le firmware
principal. Il attend les scans RFID, puis maintient par heartbeat le dernier
effet active.

Les couleurs privilegiees sont rose, bleu et violet, avec quelques variations
chaudes ou dorees pour les moments de couronnement.

## Etat de test connu

Tests deja effectues pendant le developpement :

- reset du master : OK grace au `sessionId`.
- portee avec quelques couronnes : OK.
- lecture d'un tag NFC par le master flashe : OK, reaction visible dans le
  moniteur serie.
- activation d'une couronne avec le nouveau protocole RFID : OK.
- au demarrage, les couronnes restent eteintes jusqu'a activation : OK.

Important : quand la structure de message ou les IDs d'effets changent, flasher
le master et les couronnes de test avec la meme version.

## Commandes utiles

Compiler tout :

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run
```

Compiler seulement les couronnes :

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e crown_c3
```

Compiler seulement le master :

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e master_wroom
```

Compiler seulement la remote regie :

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e remote_c3
```

## Prochaines etapes probables

- Tester les effets associes aux 12 couronnes taguees.
- Noter les effets a garder, supprimer, ralentir, adoucir ou intensifier.
- Valider avec plusieurs couronnes la synchronisation 1 seconde apres chaque
  activation.
- Valider sur scene la logique d'activation par Yara et l'ergonomie du sceptre.
- Ajouter un mode de selection d'effet alternatif cote master si necessaire.
- Ajouter eventuellement une synchronisation temporelle plus precise des phases
  d'animation entre couronnes.
- Extraire plus tard la logique reseau dans un module dedie si `node_main.cpp`
  grossit de nouveau.
- Ajouter une documentation des tags RFID et de l'ordre dramaturgique final.

## Notes pour Codex

Priorites de maintenance :

1. Ne pas casser les reglages radio valides.
2. Garder les couronnes non bloquantes : pas de `delay()` dans la logique node.
3. Preserver la limite FastLED de 5 V / 400 mA.
4. Garder `node_main.cpp` lisible et les effets dans `effects.cpp`.
5. Compiler `crown_c3` et `master_wroom` apres chaque changement structurel.
