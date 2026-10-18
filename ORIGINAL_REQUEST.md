# Original User Request

## Initial Request — 2026-10-17T00:10:57Z

Produire un rapport d'audit exhaustif et un plan de modernisation haute performance pour le moteur de jeu BetaCraft 1.7.3 (C++20 / OpenGL 3.3), proposant des améliorations concrètes sur les axes Technique, Gameplay, World Building et UX/UI, avec matrice de priorisation (Impact vs Effort) et spécifications techniques détaillées.

Working directory: /home/jngl/Documents/antigravity/excited-nobel
Integrity mode: demo

## Requirements

### R1. Audit & Modernisation Technique (Moteur & Architecture)
- Analyser l'architecture actuelle (`src/core`, `src/render`, `src/world`, `src/physics`, `src/audio`) en citant les fichiers et structures de données réelles.
- Proposer des solutions de modernisation haute performance : pipeline multithread pour la génération et le maillage des chunks (`ChunkMesher`), streaming de monde asynchrone, spatial partitioning optimisé, structure de stockage mémoire compacte des voxels, culling avancé (occlusion culling matériel / Hi-Z), et pipeline de rendu moderne (shaders post-processing, instanced rendering, transition potentielle vers abstractions graphiques plus récentes).

### R2. Évolution du Gameplay & Mécaniques
- Identifier les opportunités d'enrichissement du gameplay : système complet d'entités (architecture ECS ou polymorphisme adapté aux mobs passifs/agressifs, pathfinding 3D A* sur grille voxel), simulation de fluides dynamiques (automates cellulaires optimisés pour l'eau et la lave), logique de redstone complète (propagation de signaux sans stack overflow), et sensations de déplacement/combat (feedback d'impact, physique affinée, système d'outils et durabilité).

### R3. World Building & Génération Procédurale Avancée
- Proposer des extensions procédurales majeures : biomes étendus avec transitions naturelles et palettes climatiques, système de grottes 3D volumétriques complexes (simplex noise 3D à plusieurs octaves, rivières souterraines, failles), génération procédurale de structures préfabriquées (donjons, villages, mines abandonnées), et stratégie de rendu lointain (chunks LOD / imposteurs basse résolution pour de très grandes distances d'affichage).

### R4. UX/UI, Ergonomie & Immersion Audiovisuelle
- Proposer une refonte de l'interface utilisateur et de l'expérience joueur : UI vectorielle ou bitmap scalable (support HiDPI / GUI Scale), menus de configuration complets (mapping des touches, réglages graphiques et audio avancés), amélioration du HUD (infobulles, animations d'inventaire, retour visuel), spatialisation audio 3D avancée (atténuation réaliste, réverbération de caverne, occlusion acoustique via les blocs), et ambiances atmosphériques (effets météo, éclairage dynamique par torche tenue en main, transition ciel/brouillard).

### R5. Matrice de Priorisation & Fiches de Spécifications Techniques
- Établir une matrice de priorisation complète classant chaque recommandation selon l'Impact (1 à 5), l'Effort d'implémentation (1 à 5), le niveau de risque et les dépendances.
- Rédiger des spécifications d'ingénierie détaillées (fiches de conception technique avec pseudocode C++20, structures de données et diagrammes d'architecture Mermaid) pour les 5 chantiers jugés les plus cruciaux et à fort impact.

## Acceptance Criteria

### Exhaustivité & Ancrage dans la Codebase
- [ ] Le livrable final est généré sous forme d'un document Markdown complet et soigné à l'emplacement `docs/IMPROVEMENTS_ROADMAP.md` (avec synthèse exécutive accessible).
- [ ] Chaque axe (Technique, Gameplay, World Building, UX/UI) contient au moins 5 propositions d'amélioration distinctes, substantielles et détaillées (soit un minimum de 20 propositions documentées au total).
- [ ] Toutes les propositions s'appuient sur un diagnostic précis de l'existant en référençant les classes, fonctions et fichiers sources actuels (ex. `src/render/ChunkMesher.cpp`, `src/world/WorldGen.cpp`, `src/render/WorldRenderer.cpp`, `src/audio/AudioEngine.cpp`).

### Rigueur Technique & Spécifications
- [ ] Le rapport intègre au moins 3 diagrammes d'architecture Mermaid (ex. pipeline multithread de génération/meshing de chunks, architecture du système d'entités, pipeline de rendu/shaders).
- [ ] Les 5 chantiers prioritaires comportent des spécifications techniques de niveau ingénierie logicielle (APIs C++20 proposées, structures de données avec estimation de complexité temporelle/mémoire Big-O).

### Priorisation & Roadmap Opérationnelle
- [ ] Le document contient un tableau récapitulatif complet de matrice de priorisation (Colonnes : ID, Pilier, Titre, Impact [1-5], Effort [1-5], Priorité [P0-P3], Dépendances).
- [ ] Une roadmap par phases (Phase 1: Quick Wins / Fondations, Phase 2: Évolutions Moteur, Phase 3: Richesse de Contenu & Rendu) est clairement articulée avec estimation relative des efforts.


## Follow-up — 2026-10-17T09:39:17Z

Implémenter l'intégralité de la Phase 1 (Sprints 1.1, 1.2 et 1.3) du plan de modernisation technique de BetaCraft 1.7.3 (`docs/IMPROVEMENTS_ROADMAP.md`) en C++20 en appliquant rigoureusement la méthodologie Test-Driven Development (TDD).

Working directory: /home/jngl/Documents/antigravity/excited-nobel
Integrity mode: benchmark

## Requirements

### R1. Physique à Pas de Temps Fixe & Partitionnement Voxel en Sous-Chunks (Sprint 1.1)
- Découpler la simulation physique du framerate d'affichage via un accumulateur temporel à pas fixe (schéma de Fiedler 20 TPS / 60 TPS, P1.6) avec interpolation des transformations de rendu.
- Remplacer le stockage monolithique des colonnes de chunks par un partitionnement en sous-chunks $16 \times 16 \times 16$ avec structure `PalettedSection` alignée sur 64 octets, palette dynamique (4, 8 ou 16 bits par voxel) et allocation paresseuse de la lumière (`DenseLightArray`, P3.1).

### R2. Compression Géométrique des Sommets & Éclairage BFS Différentiel Certifié (Sprint 1.2)
- Remplacer les sommets non indexés de 40 octets par le format compact 8 octets `PackedChunkVertex` (deux mots de 32 bits stricts) combiné à un tampon d'index (EBO) partagé statique et un pavage UV fractal dans le shader (P1.2).
- Remplacer le recalcul global de 163 840 voxels lors des modifications lumineuses par un moteur d'éclairage différentiel BFS certifié avec rayon d'influence analytique $N(15) = 4\,991$ voxels maximum (`LightRemovalQueue` et `LightAdditionQueue`) et passe 1D directe par raycast pour l'extinction du skylight (P3.2).

### R3. Audio Asynchrone Sans Verrou & Modernisation de l'Interface Utilisateur (Sprint 1.3)
- Éliminer la contention et les risques de priorité inverse sur le thread audio en remplaçant `std::mutex` par une file circulaire atomique non bloquante multi-producteurs mono-consommateur `MpscRingBuffer<AudioCommand, 512>` (P4.1).
- Intégrer un rendu de texte vectoriel MSDF supportant le scaling HiDPI / GUI scale (P4.3).
- Implémenter l'écran titre, les menus d'options et le système de configuration et réassignation dynamique des touches (P4.4).

### R4. Cycle de Développement TDD & Non-Régression Stricte
- Développer chaque fonctionnalité selon le cycle TDD strict : rédaction des tests unitaires/d'intégration automatisés (phase rouge), implémentation minimale du code métier (phase verte), puis refactorisation sans dégradation.
- Maintenir une compatibilité stricte avec la baseline OpenGL 3.3 Core et la compilation C++20 sans avertissements bloquants.

## Verification Resources

- Suite de tests existante automatisée sous CTest / GoogleTest (`tests/unit/`, `tests/integration/`, `tests/e2e/`, `tests/mock/`).
- Commande de build : `cmake --build build --parallel`
- Commande de vérification globale : `ctest --test-dir build --output-on-failure`
- Fiches de spécifications techniques de référence dans `docs/IMPROVEMENTS_ROADMAP.md` (Fiche 1, Fiche 2, Fiche 5).

## Acceptance Criteria

### Physique & Chunks (Sprint 1.1)
- [ ] La simulation physique produit des trajectoires et des vitesses de déplacement identiques quel que soit le framerate de rendu (30, 60 ou 144 FPS).
- [ ] `sizeof(PalettedSection) <= 64` et l'allocation de `DenseLightArray` reste nulle (`nullptr`) pour les sections homogènes (pur air ou pure roche).
- [ ] La lecture et l'écriture de voxels via `PalettedSection` gèrent dynamiquement la transition de palette de 4 bits à 8 bits puis 16 bits.

### Rendu & Éclairage (Sprint 1.2)
- [ ] Chaque quad de maillage de chunk consomme exactement 32 octets de données de sommets (4 sommets $\times$ 8 octets) au lieu de 240 octets, vérifiable par assertions statiques (`static_assert(sizeof(PackedChunkVertex) == 8)`).
- [ ] Le tampon EBO global statique est alloué une seule fois et réutilisé pour tous les chunks du monde.
- [ ] La pose ou le retrait d'une source lumineuse (torche) ne réévalue pas plus de 4 991 voxels dans le pire des cas en espace ouvert, et met à jour les chunks adjacents sans réinitialiser 163 840 voxels.

### Audio & UI (Sprint 1.3)
- [ ] `MpscRingBuffer` permet l'insertion concurrente d'ordres audio depuis plusieurs threads sans aucun appel à `std::mutex` ni allocation dynamique dans le callback audio.
- [ ] Le menu d'options permet de reconfigurer les touches de déplacement et persiste les modifications.
- [ ] Le rendu de texte vectoriel MSDF s'adapte dynamiquement à différentes échelles de fenêtre (HiDPI / GUI Scale) sans pixelisation floue.

### Validation Globale & TDD
- [ ] De nouveaux fichiers de tests unitaires dédiés sont ajoutés dans `tests/unit/` pour chaque composant : physique fixe, `PalettedSection`, `PackedChunkVertex`, BFS lighting différentiel, `MpscRingBuffer`, MSDF/Menus.
- [ ] `ctest --test-dir build --output-on-failure` s'exécute avec 100% de tests réussis (zéro échec).
- [ ] L'application se compile proprement sous C++20 standard (`-Wall -Wextra -Wpedantic`).


## Follow-up — 2026-10-17T11:39:19Z

Le serveur Antigravity a redémarré suite à un OOM (Out Of Memory) causé par un emballement mémoire dans `DifferentialLightingEngine.cpp` (la queue BFS se propageait sans fin dans les chunks non chargés, atteignant 26,7 Go de RAM).

Ce problème a été entièrement identifié et corrigé :
1. Ajout de la garde `isVoxelInLoadedChunk` dans les 4 boucles BFS (`executeAdditionBFS`, `executeRemovalBFS`, `executeSkyAdditionBFS`, `executeSkyRemovalBFS`) et les méthodes associées de `DifferentialLightingEngine.cpp`.
2. Correction de la propagation d'extinction du skylight dans `onSkylightColumnObstructed` et `onBlockPlaced`.
3. Correction des dépendances de liaison GTest/GMock pour `adversarial_m2_stress` dans `CMakeLists.txt`.
4. Validation : 100% des tests `ctest` et les stress tests `adversarial_m2_stress` passent avec succès en 3,44s, avec une empreinte RAM maximale réduite de 26,7 Go à 76 Mo.

Veuillez reprendre le workflow de l'équipe : valider la porte du Milestone 2 (Sprint 1.2) et lancer l'implémentation du Milestone 3 (Sprint 1.3 : MpscRingBuffer audio sans verrou, rendu UI MSDF et menus/options).
