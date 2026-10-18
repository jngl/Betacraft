# BetaCraft 1.7.3 — Clone de Minecraft Beta 1.7.3 (C++20 / OpenGL 3.3 / SDL2 / miniaudio)

Clone autonome et **production-ready** de **Minecraft Beta 1.7.3**, développé en **C++20**, **OpenGL 3.3 Core Profile**, **SDL2** et **miniaudio**.

## Fonctionnalités principales (Fidélité Beta 1.7.3)

- **Génération procédurale Beta 1.7.3 (`WorldGen`)** :
  - Carte climatique **Température × Humidité** générant 10 biomes (`Rainforest`, `Seasonal Forest`, `Forest`, `Taiga`, `Savanna`, `Shrubland`, `Plains`, `Swampland`, `Desert`, `Tundra`) avec teintes de l'herbe et du feuillage interpolées par colonne.
  - Relief 3D avec surplombs (overhangs), falaises, plages de sable/gravier/argile au niveau de la mer (`Y = 64`), grottes 3D et filons de minerais (Charbon, Fer, Or, Diamant, Redstone, Lapis-Lazuli).
  - Arbres (Chênes, Bouleaux, Pins), hautes herbes (*Tall Grass* introduites en Beta 1.6/1.7), fleurs (Pissenlits, Roses), cactus et cannes à sucre.
- **Éclairage & Rendu (`WorldRenderer`, `ChunkMesher`, `SkyRenderer`)** :
  - **Smooth Lighting** (Ambient Occlusion par sommet + interpolation de la lumière du ciel `0..15` et de la lumière des blocs `0..15` émise par les torches et la glowstone).
  - Cycle Jour/Nuit dynamique avec Soleil, Lune, champ d'étoiles, nuages cubiques à `Y = 108` et brouillard atmosphérique.
  - Rendu deux passes (Opaque/Alpha-Cutout + Translucide pour l'eau et la glace) avec **Frustum Culling**.
  - Fissures progressives de minage (10 étapes), particules texturées de destruction et main/outil/bloc 3D à la première personne avec *view-bobbing*.
- **Mécanique des Pistons Beta 1.7 (`World::triggerPistonPush`)** :
  - Clic droit sur un **Piston** pour pousser jusqu'à 12 blocs dans la direction opposée à la face cliquée.
- **Physique & Survie (`Player`, `Raycaster`, `AABB`)** :
  - Résolution de collisions AABB par axe, gravité, nage dans l'eau, mode vol (`F`), et **protection de rebord en mode Sneak (`LShift`)** fidèle à la Beta 1.7.3.
  - Barre de vie (10 cœurs avec dégâts de chute), bulles d'oxygène sous l'eau, inventaire 36 emplacements et système de **Crafting** (`E`).
- **Audio procédural (`AudioEngine` via `miniaudio`)** :
  - Synthèse sonore polyphonique temps réel (pas de marche, minage, destruction, pose de blocs par matériau, éclaboussures d'eau, piston mécanique, clic UI et musique ambiante inspirée de C418 activable avec `M`).
- **Assets 100% autonomes (`TextureAtlas`)** :
  - Génère au démarrage un atlas pixel-art 16×16 (`256×256` RGBA) fidèle au `terrain.png` de la Beta 1.7.3 et l'exporte dans `assets/terrain.png` (ou charge automatiquement tout `assets/terrain.png` personnalisé).
- **Persistance (`WorldStorage`)** :
  - Sauvegarde automatique et manuelle (`F5`) des chunks modifiés (compression RLE binaire) et des métadonnées du monde (`saves/world1/level.dat`).

## Contrôles

| Touche / Souris | Action |
|---|---|
| `Z/Q/S/D` ou `W/A/S/D` | Se déplacer (compatible AZERTY et QWERTY) |
| `Espace` | Sauter / Nager / Monter (en vol) |
| `Shift Gauche` | S'accroupir (*Sneak* — empêche de tomber des bords) / Descendre (en vol) |
| `Ctrl Gauche` | Courir (*Sprint*) |
| `Clic Gauche` (maintenir) | Miner / Détruire un bloc |
| `Clic Droit` | Poser un bloc / Activer un Piston Beta 1.7 / Ouvrir l'Établi |
| `Clic Molette` | Pipette (*Pick Block* dans la barre d'action) |
| `1..9` ou `Molette` | Sélectionner l'emplacement de la barre d'action |
| `E` ou `I` | Ouvrir/Fermer l'Inventaire, le Crafting et la Palette de blocs |
| `F` | Activer/Désactiver le mode Vol (*Fly*) |
| `L` | Activer/Désactiver le *Smooth Lighting* |
| `M` | Activer/Désactiver la musique ambiante |
| `T` | Avancer l'heure du cycle Jour/Nuit |
| `F3` | Afficher/Masquer l'écran de debug Beta 1.7.3 |
| `F5` | Sauvegarder le monde manuellement |
| `F11` | Basculer en mode Plein Écran |
| `Échap` | Libérer la souris / Fermer l'inventaire / Quitter (sauvegarde auto) |

## Compilation et Tests

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/betacraft
```
