# Free Camera per Pokémon Alpha Sapphire (3gx)

Plugin `.3gx` derivato da [Sango Plugin](https://github.com/David-Darras/sango-plugin)
(David Darras, GPL‑3.0). Di Sango resta **solo** l'avvio del plugin, il sistema di
hook e le funzioni per sostituire la telecamera del gioco. Tutto il resto
(menu, editor, strumenti, ecc.) è stato tolto.

## Cosa fa

Nell'overworld la telecamera **orbita attorno al personaggio** e lo segue quando
cammina, ma **non ruota mai da sola**: l'angolo lo cambi solo tu.
Il movimento con lo **stick analogico sinistro** è **relativo alla telecamera**:
"su" porta il personaggio lontano da te, qualunque sia l'angolo, e se giri la
visuale mentre cammini il personaggio gira con lei.
La croce direzionale (movimento a passi) non segue la telecamera.

| Comando | Azione |
|---|---|
| Stick destro (C‑Stick) | Gira la telecamera (destra/sinistra) e la inclina (su/giù) |
| L / R | Gira a sinistra / destra (utile senza stick destro) |
| ZL / ZR | Abbassa / alza la telecamera (New 3DS) |
| L + R insieme | Rimette la telecamera dietro al personaggio |

Le battaglie e le altre schermate usano la telecamera normale del gioco.

La telecamera può girare al massimo di circa 70° rispetto alla visuale normale
del gioco: le mappe sono costruite come un palcoscenico e il **retro delle case
non esiste** come modello. Per girare di 360° metti `kYawLimit = 3.14159f`
(vedrai però i retri vuoti).

## Come ottenere il file .3gx (senza installare nulla)

1. Crea un account su <https://github.com> e un nuovo repository (anche privato).
2. Carica **tutto il contenuto** di questa cartella nel repository
   (compresa la cartella nascosta `.github`).
3. Apri la scheda **Actions**. Parte da sola la corsa **Build**
   (se chiede di abilitare i workflow, abilitali).
4. Quando compare la spunta verde (qualche minuto), aprila e in fondo, in
   **Artifacts**, scarica **freecam-plugin**: dentro c'è `freecam.3gx`.
5. Se compare una croce rossa, apri il log e mandami l'errore.

In alternativa, con devkitPro e 3gxtool installati: `make` nella cartella.

## Installazione (Azahar)

1. Metti `freecam.3gx` in `sdmc/luma/plugins/000400000011C500/`
   (la cartella `sdmc` è quella di Azahar). **Un solo file .3gx** nella cartella:
   togli `sango_plugin.3gx` se c'è.
2. In Azahar attiva il caricatore plugin: Configurazione → Sistema →
   *Abilita il caricatore di plugin 3GX*.
3. Avvia Alpha Sapphire **v1.4** e mappa lo stick destro sul C‑Stick.
   Il C‑Stick funziona se l'emulatore è in modalità New 3DS.

Su console con Luma3DS: stessa cartella, poi L + Giù + Select → Plugin Loader.

## Regolare la telecamera

All'inizio di `src/free_camera.cc` ci sono le impostazioni: distanza
(`kDistance`), angolo iniziale (`kStartPitch`), velocità di rotazione
(`kYawSpeed`, `kPitchSpeed`), zona morta, Y invertito (`kInvertPitch`).
Dopo averle cambiate, ricompila.

## Cose da sapere (stato del lavoro)

- Il codice **non è stato compilato per 3DS né provato nel gioco** da chi l'ha scritto:
  è stato controllato solo con un compilatore di prova su PC (sintassi,
  simboli del link e un test della matematica in `tests/`).
- Il C‑Stick si legge con il servizio di sistema `ir:rst`. Se non è disponibile,
  restano L, R, ZL e ZR.
- Per camminare relativi alla telecamera il plugin ruota la posizione dello stick
  che il gioco legge mentre calcola il movimento. Se il personaggio non segue la
  telecamera, prova `kTurnStickEverywhere = true` in `src/free_camera.cc`
  (ma poi i menu nell'overworld vedranno lo stick ruotato).
  Bici, surf e altre andature potrebbero comportarsi in modo diverso.
- La telecamera non evita i muri: dentro le case può attraversarli.

## Licenza e crediti

GNU GPL v3 (vedi `LICENSE`), come Sango Plugin.
Basato su Sango Plugin di David Darras; l'avvio del plugin viene da
CTRPluginFramework (ThePixellizerOSS). Pokémon è un marchio di Nintendo,
Game Freak e The Pokémon Company. Questo progetto non contiene il gioco.
