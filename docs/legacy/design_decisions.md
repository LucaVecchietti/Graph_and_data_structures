# Legacy log — Design decisions

> Architectural decisions taken so far, the reasoning behind them, and the trade-offs accepted.

| Campo | Valore |
|---|---|
| Tipo | legacy-decisions |
| Lingua | en |
| Ultimo aggiornamento | 2026-06-13 |
| Commit di riferimento | cb9939c |
| Mirror | — |

---

## Indice

- [2026-06-13 — Reuse of the rel/edges freelist bins (edge-space compaction)](#2026-06-13--reuse-of-the-reledges-freelist-bins-edge-space-compaction)
- [2026-06-07 — Bin per-tipo per i record COMPLEX via prog_number zero-paddato](#2026-06-07--bin-per-tipo-per-i-record-complex-via-prog_number-zero-paddato)
- [2026-06-07 — Indice inverso degli archi entranti in-RAM](#2026-06-07--indice-inverso-degli-archi-entranti-in-ram)
- [2026-06-07 — Tombstone + azzeramento delle regioni su delete](#2026-06-07--tombstone--azzeramento-delle-regioni-su-delete)
- [2026-06-03 — Freelist a bin segregati per dimensione esatta + cancellazione nodo](#2026-06-03--freelist-a-bin-segregati-per-dimensione-esatta--cancellazione-nodo)
- [2026-06-02 — Id arco globale: sorgente in `MetaRecord.next_edge_id`, memorizzato in `EdgeRef`](#2026-06-02--id-arco-globale-sorgente-in-metarecordnext_edge_id-memorizzato-in-edgeref)
- [2026-05-30 — Edge persistence: append + obsolete + in-place index patch](#2026-05-30--edge-persistence-append--obsolete--in-place-index-patch)
- [2026-05-26 — Storage sidecar JSON per nodi COMPLEX](#2026-05-26--storage-sidecar-json-per-nodi-complex)
- [2026-05-26 — Introduzione tag NodeType::COMPLEX + ComplexRecord (WIP)](#2026-05-26--introduzione-tag-nodetypecomplex--complexrecord-wip)
- [2026-05-26 — Separazione POD vs Domain struct](#2026-05-26--separazione-pod-vs-domain-struct)
- [2026-05-26 — Type-erased BaseNode + Node\<T\>](#2026-05-26--type-erased-basenode--nodet)
- [2026-05-26 — Policy-based traversal (BFS/DFS)](#2026-05-26--policy-based-traversal-bfsdfs)
- [2026-05-26 — Append-only data files, truncated meta](#2026-05-26--append-only-data-files-truncated-meta)
- [2026-05-26 — Single-open append su nodes.dat](#2026-05-26--single-open-append-su-nodesdat)
- [2026-05-26 — POD packed e fragilità ABI](#2026-05-26--pod-packed-e-fragilità-abi)
- [2026-05-26 — Hash table standalone in C (non linkata)](#2026-05-26--hash-table-standalone-in-c-non-linkata)

---

### 2026-06-13 — Reuse of the rel/edges freelist bins (edge-space compaction)

- **Stato:** active
- **Contesto:** Dal 2026-06-07 ([BUG-017](known_bugs.md#2026-06-07--bug-017-update_node_edges-orfanizza-regioni-senza-spingerle-sulla-freelist)) `update_node_edges` spinge la vecchia `RelationNodeList` sul bin `rel` e ogni vecchio chunk di edge sul bin `edges`, li azzera e incrementa `free_edge_count` — ma quei bin non venivano **riusati** da nessun writer (il reuse di `insert` toccava solo i bin `nodes`/`complex`). Risultato: ad ogni `add_edge` (incluso un semplice overwrite del peso di un arco già esistente) `nodes.dat`/`edges.dat` crescevano comunque in modo monotono, e le regioni liberate si accumulavano sui bin senza mai rientrare. Era il primo TODO della ROADMAP (edge-space compaction) ed è anche la conseguenza ancora aperta della [decisione freelist 2026-06-03](#2026-06-03--freelist-a-bin-segregati-per-dimensione-esatta--cancellazione-nodo).
- **Decisione:** Cablare lo **step 3** di `update_node_edges` (scrittura della NUOVA regione relation-list e dei NUOVI chunk di edge) su una strategia **pop-then-append**, simmetrica al reuse di `insert`. Prima di appendere a EOF si tenta un `pop_free_offset` sul bin segregato di dimensione **esatta**: bin `rel` per la regione relation-list di size `sizeof(RelationNodeList) + batch_size`, bin `edges` per ogni chunk di size `edge_count * sizeof(Edge)`. Se il bin restituisce un buco, lo si riusa in place (`seekp` all'offset liberato); altrimenti si appende a fine file come prima. Gli stream `nodes.dat`/`edges.dat` passano da `std::ios::app` a `std::ios::in | std::ios::out`, così `seekp` atterra in place sul reuse e continua a estendere il file sull'append. Lo step 2 (push delle vecchie regioni sui bin + azzeramento) e lo step 4 (patch in-place di `NodeIndex.relation_offset`) restano invariati.
- **Insight portante (ordering step 2 → step 3):** lo step 2 fa il **push** delle vecchie regioni sui bin, poi lo step 3 fa il **pop**. Su un overwrite del peso, la nuova relation-list e ogni nuovo chunk di edge sono **byte-identici in size** a quelli appena liberati, quindi i bin LIFO segregati per size restituiscono **esattamente** quelle regioni → vero overwrite in place, **zero crescita** del file, e `free_edge_count` round-trippa (gli zeri scritti nello step 2 vengono sovrascritti nello step 3 — innocui). Il fit esatto è garantito dal modello a bin per size già esistente.
- **Alternative considerate:**
  - *Pass di compaction offline/standalone* (mark-and-sweep o riscrittura periodica dei file): scartata — più complessa, richiede re-indicizzare i file interi, e non dà alcun vantaggio in place sul caso comune (l'overwrite di un arco), dove il pop-then-append riusa il buco appena liberato senza alcuna crescita.
  - *Riciclo degli id arco* (riusare `BatchOfEdgesFreeOffset.idx` del chunk poppato): scartata per ora — ogni `Edge` conserva il proprio `EdgeRef.id` (id globale stabile, vedi [decisione 2026-06-02](#2026-06-02--id-arco-globale-sorgente-in-metarecordnext_edge_id-memorizzato-in-edgeref)); il campo `idx` del record poppato viene ignorato.
- **Conseguenze:**
  - Solo il pop dal bin `edges` decrementa `meta.free_edge_count` (specularmente allo step 2, che lo incrementa solo per i chunk di edge). Il bin `rel` non ha un contatore dedicato — stessa contabilità di `delete_node_from_disk`.
  - Reuse **solo a fit esatto** (opportunistico), coerente col modello del node freelist: niente best-fit, niente split, niente slack.
  - Nessun cambio di POD/schema → **nessun wipe di `db/`** richiesto. Le firme restano invariate (vedi [API change](api_changes.md#2026-06-13--update_node_edges-step-3-append-only--pop-then-append)).
  - **Boundary residuo:** l'append a insert-time (`write_node` → `write_relation_node_list` per un nodo fresco) continua ad appendere, ma scrive una relation-list **vuota** (header con `batch_size = 0`) + zero edge, quindi la crescita è trascurabile. La compaction copre il percorso di **rewrite** (`update_node_edges`), non l'insert iniziale.
  - L'indice inverso (`build_inbound_index`) non è toccato: il chunk riusato appartiene al nodo che lo sta riscrivendo, nessun riferimento dangling.
- **Riferimenti:** `graph_core/io/graph_io.cpp` `update_node_edges` step 3 (`pop_free_offset` su bin `rel`/`edges` + `seekp` in place, stream `in|out`); `main.cpp` Phase 5 (regression guard). Vedi [BUG-017](known_bugs.md#2026-06-07--bug-017-update_node_edges-orfanizza-regioni-senza-spingerle-sulla-freelist) (step push, stessa famiglia) e [Freelist a bin segregati](#2026-06-03--freelist-a-bin-segregati-per-dimensione-esatta--cancellazione-nodo).

---

### 2026-06-07 — Bin per-tipo per i record COMPLEX via prog_number zero-paddato

- **Stato:** active
- **Contesto:** La freelist a bin segregati (vedi [decisione 2026-06-03](#2026-06-03--freelist-a-bin-segregati-per-dimensione-esatta--cancellazione-nodo)) assume regioni di **size esatta**: pop = fit esatto, scrittura in place senza slack. I primitivi hanno 3 size ({1,4,8}). Un record COMPLEX vale invece `R = sizeof(ComplexHeader) + type_label_size + json_file_path_size`, con `json_file_path = "{prog_number}_{type_label}.json"` → `R = 22 + 2·L + d` (L = lunghezza `type_label`, d = cifre di `prog_number`). La varianza ha due sorgenti: **L** (intrinseca al tipo) e **d** (incidentale all'istanza, cresce di 1 byte ad ogni decade del contatore). Con size quasi-continua i bin esatti esplodono (un file per byte-size) e l'hit-rate di riuso crolla.
- **Decisione:** Azzerare la sorgente incidentale **zero-paddando `prog_number` a larghezza fissa** `COMPLEX_PROG_DIGITS = 20` (copre l'intero range `uint64`). Così `json_file_path_size` diventa costante per un dato tipo e `R = (22 + COMPLEX_PROG_DIGITS) + 2·L` dipende **solo da L**: tutti i record di un tipo (o di tipi con lo stesso `L`) hanno la stessa size esatta, e i bin esatti esistenti `complex_<R>.dat` funzionano automaticamente come **classi di size per-tipo** — nessuna funzione di bucketing né padding del record. Helper `complex_record_on_disk_size(L)` calcola `R` prima della scrittura. Il sidecar JSON è gestito a parte: ogni nodo COMPLEX è **un file** (nessun offset), quindi su delete si fa `remove()` del file e si ricicla il `prog_number` su una **json free list** (`db/freelist/json_prog.dat`, stack LIFO di `uint64`); `complex_node_to_record` ripesca un numero riciclato prima di consumarne uno nuovo (chiude anche [BUG-014](known_bugs.md#2026-05-26--bug-014-prog_number-mai-incrementatopersistito-dopo-write-complex)). Il reuse in place è gestito da `write_complex_in_freed_slot`.
- **Alternative considerate:**
  - *Bin per classe di size con padding del record (ladder lineare/potenze di 2)*: avrebbe richiesto padding e gestione dello slack; lo zero-padding del filename sposta lo "spreco" in qualche byte di nome (sidecar) invece che in byte morti dentro `nodes.dat`, e mantiene i bin esatti.
  - *Bin senza padding + best-fit con scan su freelist unica*: scartata — perde il pop O(1) e introduce leak della "coda" sotto churn (il record migrerebbe in una classe più piccola ad ogni ciclo).
  - *`Dbudget` più piccolo (es. 10 cifre)*: nomi più corti ma richiede un guard sull'overflow del contatore; con 20 cifre l'overflow su `uint64` è impossibile, nessun guard.
- **Conseguenze:**
  - I filename dei sidecar sono zero-paddati (es. `00000000000000000000_Athlete.json`); ~+19 byte di path per record COMPLEX rispetto al minimo, deterministici.
  - Due tipi **diversi con lo stesso `L`** condividono il bin: è corretto e desiderato (uno slot liberato è riusabile da qualunque record della stessa size).
  - Nuovo bin-prefix `complex` e nuovo file `db/freelist/json_prog.dat`. `node_record_payload_size(COMPLEX)` resta header-only ma non è più sul percorso di delete (la size reale si legge dall'header).
  - `write_complex` ora prende `std::ostream &` (non più `std::ofstream &`) per poter scrivere in place su `std::fstream` (vedi [API change](api_changes.md#2026-06-07--write_complex-su-stdostream--write_complex_in_freed_slot)).
- **Riferimenti:** commit `9966603`. `graph_core/costants.h` (`COMPLEX_PROG_DIGITS`), `graph_core/odt/node_odt.cpp` (`complex_node_to_record`), `graph_core/io/graph_io.h` (`complex_record_on_disk_size`, `json_freelist_path`, `write_complex_in_freed_slot`), `graph_core/io/graph_io.cpp` (ramo COMPLEX di `delete_node_from_disk`), `graph_core/graph.h` (ramo COMPLEX del reuse di `insert`).

---

### 2026-06-07 — Indice inverso degli archi entranti in-RAM

- **Stato:** active
- **Contesto:** Il modello è diretto e **solo-uscente**: l'adiacenza è memorizzata dal lato sorgente (`BaseNode::neighborgs[relation][to_id]`). Cancellare un nodo X deve rimuovere anche gli archi **entranti** (di altri nodi che puntano a X), altrimenti restano vicini dangling che dopo un reload puntano a uno slot tombstonato. Ma "chi punta a X?" non è interrogabile: nessun indice per `to_node`. Il POD `Edge` ha `from_node`, quindi da un arco si risale al proprietario, ma serve un modo di trovarli senza scansionare tutto `edges.dat` ad ogni delete.
- **Decisione:** Indice inverso **in-RAM**, mai persistito, ricostruito al load. `Graph::in_edges` è una `unordered_map<int to_id, unordered_set<int from_id>>`. `build_inbound_index(next_id)` lo costruisce con uno scan O(N+E) che segue **solo i nodi vivi** (salta i `TOMBSTONE`) leggendo le loro `RelationNodeList` → quindi non legge mai chunk azzerati/liberi (evita falsi positivi su edge a zero). È mantenuto incrementale da `add_edge` (`in_edges[end].insert(start)` su arco nuovo) e da `delete_node`. Su delete di X: (a) per ogni vicino uscente si toglie X dalla loro inbound set; (b) per ogni proprietario entrante (da `in_edges[X]`) si carica il nodo, si rimuove X da ogni relazione e si ri-persiste via `update_node_edges`; (c) si decrementa `edge_count` di conseguenza.
- **Alternative considerate:**
  - *Scan di `edges.dat` ad ogni delete (nessuna struttura)*: minima da implementare, ma O(E) per cancellazione e va gestita la trappola degli edge azzerati.
  - *Reverse adjacency persistita su disco*: delete O(deg_in) ma raddoppia le scritture e tocca il formato on-disk (fragile).
  - *Indice inverso in-RAM (scelta)*: uno scan O(N+E) una volta al load, poi delete in O(deg_in); nessun cambio di schema, struttura volatile.
- **Conseguenze:**
  - Il costruttore di `Graph` ora fa uno scan O(N+E) al load (prima leggeva solo `meta`). Per il prototipo (grafi piccoli) accettabile.
  - `delete_node` rimuove e ri-persiste i nodi proprietari → nessun vicino dangling dopo reload. I loro **vecchi** chunk vengono orfanati da `update_node_edges`, che dal 2026-06-07 ([BUG-017](known_bugs.md#2026-06-07--bug-017-update_node_edges-orfanizza-regioni-senza-spingerle-sulla-freelist)) li spinge sui bin `rel`/`edges`, li azzera e incrementa `free_edge_count`. I bin non erano ancora **riusati** alla data di questa decisione, quindi le regioni si accumulavano ma erano tracciate. *(Aggiornamento 2026-06-13: lo step 3 di `update_node_edges` ora riusa i bin `rel`/`edges` a fit esatto — vedi [Reuse of the rel/edges freelist bins](#2026-06-13--reuse-of-the-reledges-freelist-bins-edge-space-compaction).)*
  - `edge_count` ora viene davvero decrementato sul delete (prima restava invariato).
- **Riferimenti:** commit `1315b00`. `graph_core/graph.h` (`in_edges`, `build_in_edges`), `graph_core/graph.cpp` (`Graph::delete_node`, `add_edge`, costruttore), `graph_core/io/graph_io.cpp` / `graph_core/io/graph_io.h` (`build_inbound_index`).

---

### 2026-06-07 — Tombstone + azzeramento delle regioni su delete

- **Stato:** active
- **Contesto:** Il prototipo di `delete_node` ([decisione 2026-06-03](#2026-06-03--freelist-a-bin-segregati-per-dimensione-esatta--cancellazione-nodo)) spingeva le regioni del nodo sui bin ma lasciava lo slot in `nodes.idx` intatto (puntava a byte ormai morti) e i byte del record non azzerati. Una `read_node` su un id cancellato (anche un vicino dangling) avrebbe riletto spazzatura finché lo slot non veniva riusato.
- **Decisione:** Nuovo tag `NodeType::TOMBSTONE = 254`. Su delete, `delete_node_from_disk` (1) **azzera** su disco la regione `NodeRecord`, la `RelationNodeList` e ogni chunk di edge (helper `zero_region`, niente byte morti / niente leak), (2) **tombstona** lo slot scrivendo un `NodeIndex` con `type_id = TOMBSTONE` e offset azzerati, conservando l'`id` (lo slot resta riusabile via freelist). `read_node` su uno slot `TOMBSTONE` lancia (fallimento rumoroso invece di spazzatura silenziosa — sinergia con la pulizia degli archi entranti). Un reuse successivo (`write_node_in_freed_slot` / `write_complex_in_freed_slot`) riscrive l'intero `NodeIndex` in place, cancellando naturalmente il tombstone.
- **Alternative considerate:**
  - *Azzerare l'intero slot idx*: ambiguo, perché `id = 0` è un nodo valido.
  - *Non tombstonare, affidarsi solo alla freelist*: lascia lo slot a puntare a byte morti fino al reuse — è proprio il sintomo da chiudere.
- **Conseguenze:**
  - `NodeType` guadagna 254; `read_node` ha un `case NodeType::TOMBSTONE` che lancia. `node_record_payload_size(TOMBSTONE)` cade nel `default` (throw) ma non è mai chiamato su un tombstone (la size si calcola dal tipo originale prima di tombstonare).
  - Nessun cambio di layout POD (`NodeIndex`/`MetaRecord` invariati): i `db/` esistenti restano compatibili, è solo un nuovo valore enum.
- **Riferimenti:** commit `b56e86e`. `graph_core/struct/pod_struct.h` (`NodeType::TOMBSTONE`), `graph_core/io/graph_io.cpp` (`zero_region`, ramo tombstone di `delete_node_from_disk`, `case TOMBSTONE` in `read_node`).

---

### 2026-06-03 — Freelist a bin segregati per dimensione esatta + cancellazione nodo

- **Stato:** active (il prototipo `delete_node` è stato completato il 2026-06-07 — [BUG-016](known_bugs.md#2026-06-03--bug-016-delete_node-prototipo-non-aggiorna-idx-contatori-meta-archi-entranti-complex) chiuso: vedi [tombstone](#2026-06-07--tombstone--azzeramento-delle-regioni-su-delete), [archi entranti](#2026-06-07--indice-inverso-degli-archi-entranti-in-ram), [binning COMPLEX](#2026-06-07--bin-per-tipo-per-i-record-complex-via-prog_number-zero-paddato). Le "Conseguenze" sul prototipo qui sotto restano per contesto storico.)
- **Contesto:** Dal 2026-05-30 ogni `add_edge` orfanizza la vecchia `RelationNodeList` e i vecchi chunk di edge, e il DB cresce monotono (vedi [Edge persistence](#2026-05-30--edge-persistence-append--obsolete--in-place-index-patch)). Mancavano due cose: (a) una cancellazione di nodo, e (b) il meccanismo di reclamo dello spazio orfano promesso da quel design. La vecchia POD `FreeRecord` (un solo `uint64_t offset`) e `MetaRecord.free_count` erano placeholder mai usati: un solo offset senza la **dimensione** della regione non permette di riusare un buco in sicurezza.
- **Decisione:** Freelist persistente a **bin segregati per dimensione esatta**: un file per ogni size distinta di regione libera, sotto `db/freelist/<prefix>_<size>.dat`, con `prefix ∈ {nodes, rel, edges}`. La size è codificata nel nome del file, quindi:
  - **push** (su delete/orphan) = append di un record in fondo al bin giusto → O(1);
  - **pop** (su insert/reuse) = legge l'ultimo record e tronca il file di un record (`resize_file`) → O(1), LIFO.
  Ogni record di un bin ha la stessa size, quindi un pop è **sempre un fit esatto**: niente scan, niente rewrite del file, niente byte sprecati. Tre nuove POD sostituiscono `FreeRecord`: `NodeFreeOffset {idx, offset, size}` (porta anche l'id riusabile dello slot in `nodes.idx`), `RelationNodeListFreeOffset {offset, size}`, `BatchOfEdgesFreeOffset {idx, offset, size}` (un intero chunk di edge contigui, `idx` = id del primo arco del batch). `Graph::delete_node` spinge le tre regioni del nodo sui bin via `write_free_offset` + `freelist_bin_path`; `Graph::insert` — **solo per i primitivi a size fissa**, mai per COMPLEX — prova un `pop_free_offset` sul bin `nodes_<size>` e, se trova un buco, riusa id e regione via `write_node_in_freed_slot` invece di appendere (`next_id` non avanza, `node_count` sì).
- **Alternative considerate:**
  - *Singola freelist flat (`FreeRecord`, un solo offset) con first-fit*: scartata — senza size ogni reuse dovrebbe scansionare e rischierebbe fit parziali con frammentazione interna.
  - *Freelist unica con `(offset, size)` e best-fit*: scartata per ora — best-fit richiede ordinamento/scan O(n); i bin per size danno fit esatto in O(1), al costo di molti file piccoli.
  - *Mark-and-sweep / compaction periodica*: rimandata — richiede riscrivere e re-indicizzare i file interi.
- **Conseguenze:**
  - Nuova sottocartella `db/freelist/` con un file per classe di size (creata on-demand da `write_free_offset`). Tanti file piccoli, uno per size class.
  - LIFO: l'ultima regione liberata di una data size è la prima riusata. Località temporale, ma nessuna garanzia di compattazione.
  - `delete_node` è un **prototipo**: registra le regioni orfane sui bin ma NON tombstona lo slot in `nodes.idx`, NON aggiorna i contatori `meta` (`node_count`/`free_count` → `meta` resta `const &`), NON rimuove gli archi entranti da altri nodi (vicini dangling), NON gestisce la size variabile del payload COMPLEX (`node_record_payload_size` per COMPLEX ritorna solo `sizeof(ComplexHeader)`). Vedi [BUG-016](known_bugs.md#2026-06-03--bug-016-delete_node-prototipo-non-aggiorna-idx-contatori-meta-archi-entranti-complex).
  - `update_node_edges` spinge le regioni orfane sui bin `rel`/`edges` dal 2026-06-07 ([BUG-017](known_bugs.md#2026-06-07--bug-017-update_node_edges-orfanizza-regioni-senza-spingerle-sulla-freelist)): non c'è più leak non tracciato su `add_edge`. Dal 2026-06-13 lo step 3 di `update_node_edges` **riusa** anche i bin `rel`/`edges` a fit esatto (pop-then-append): su un overwrite del peso il file non cresce più — vedi [Reuse of the rel/edges freelist bins](#2026-06-13--reuse-of-the-reledges-freelist-bins-edge-space-compaction).
  - `FreeRecord` rimossa dal POD layout (vedi [API change](api_changes.md#2026-06-03--freerecord-rimossa-sostituita-da-tre-pod-free-offset)). I riferimenti "FreeRecord / free_count riservati" nelle decisioni precedenti ([Append-only](#2026-05-26--append-only-data-files-truncated-meta), [Edge persistence](#2026-05-30--edge-persistence-append--obsolete--in-place-index-patch)) vanno letti come superati da questa.
- **Riferimenti:** commit `ca567e4`. `graph_core/struct/pod_struct.h:182,196,209` (le tre POD), `graph_core/io/graph_io.h:321` (`freelist_bin_path`), `:333` (`write_free_offset`), `:355` (`pop_free_offset`), `:190` (`write_node_in_freed_slot`), `graph_core/io/graph_io.cpp:380` (`delete_node_from_disk`), `graph_core/graph.cpp:163` (`Graph::delete_node`), `graph_core/graph.h:43` (reuse path di `insert`), `graph_core/struct/type_registry.cpp` (`node_record_payload_size`).

---

### 2026-06-02 — Id arco globale: sorgente in `MetaRecord.next_edge_id`, memorizzato in `EdgeRef`

- **Stato:** active
- **Contesto:** Chiusura di [BUG-002](known_bugs.md#2026-05-26--bug-002-edgeid-non-globale-tra-nodi): `Edge.id` non era univoco perché veniva generato da un contatore locale azzerato per ogni nodo. La difficoltà è che `update_node_edges` riscrive **tutti** gli archi del nodo ad ogni `add_edge`; se gli id si rigenerassero ad ogni riscrittura non sarebbero né stabili né globali, e un contatore che parte da `next_edge_id` crescerebbe di `N` (numero archi del nodo) ad ogni chiamata. Perché un arco esistente conservi il suo id quando viene riscritto, l'id deve essere **ricordato**.
- **Decisione:** L'id dell'arco è globale e stabile. Sorgente unica: `MetaRecord.next_edge_id` (monotona, persistita su `meta.dat`). L'id è memorizzato in RAM nella struct di dominio `EdgeRef { uint64_t id; int weight; BaseNode* neighbor; }`, che sostituisce la vecchia `std::pair<int, BaseNode*>` come valore della mappa di adiacenza. Flusso in `add_edge`: un arco **nuovo** consuma `id = next_edge_id` e incrementa `next_edge_id` + `edge_count`; un arco **esistente** riusa il proprio `id` (si sovrascrive solo il peso). I loop di scrittura emettono `EdgeRef.id` nel POD `Edge`; il read path ricostruisce `EdgeRef` con `Edge.id` letto da disco, così l'id sopravvive a reload e riscritture complete del nodo.
- **Alternative considerate:**
  - *Contatore che parte da `next_edge_id` dentro `update_node_edges`, senza memorizzare l'id*: scartata — riassegna gli id ad ogni riscrittura del nodo (non stabili) e gonfia `next_edge_id`.
  - *`std::tuple<uint64_t,int,BaseNode*>` invece di una struct nominata*: scartata per leggibilità — i campi nominati (`.id/.weight/.neighbor`) sostituiscono i vecchi `.first/.second` in modo più chiaro dei `std::get<N>`.
- **Conseguenze:**
  - Schema break su `meta.dat` (24 → 48 byte): vedi [API change MetaRecord](api_changes.md#2026-06-02--metarecord-aggiunti-i-campi-edge_count-next_edge_id-free_edge_count).
  - Il POD `Edge` su disco è invariato: cambia solo la **semantica** del campo `id` (ora globale).
  - Tutti i siti che usavano `.first/.second` sull'arco aggiornati (vedi [API change neighborgs](api_changes.md#2026-06-02--basenodeneighborgs-da-pairint-basenode-a-edgeref)).
  - `next_edge_id` cresce di 1 per arco realmente nuovo; un overwrite non lo tocca ma riscrive comunque il salvataggio dell'arco (comportamento richiesto).
  - Senza freelist, gli archi cancellati (non ancora implementati) non riuseranno gli id: `next_edge_id` resta monotona.
- **Riferimenti:** commit `309d3f9`. `graph_core/struct/domain_struct.h:24` (`EdgeRef`), `graph_core/struct/pod_struct.h:136` (`MetaRecord`), `graph_core/graph.cpp:57` (`add_edge`), `graph_core/io/graph_io.h:105,236`, `graph_core/io/graph_io.cpp:344`.

---

### 2026-05-30 — Edge persistence: append + obsolete + in-place index patch

- **Stato:** active
- **Contesto:** Prima del 2026-05-30, `Graph::add_edge` mutava solo l'adiacenza in RAM. Al riavvio del processo l'arco era perso ([BUG-001](known_bugs.md#2026-05-26--bug-001-add_edge-non-persiste-su-disco)). Il design della `RelationNodeList` ha una **coda di lunghezza variabile** (`type_count` entry, ciascuna con `name` di lunghezza variabile + due offset), e i chunk di edge in `edges.dat` sono memorizzati **contigui per coppia `(node, relation)`**. Aggiungere un edge a una relazione esistente non si può fare in-place: la `RelationNodeList` cambierebbe dimensione e il chunk contiguo perderebbe la contiguità.
- **Decisione:** Strategia "append + obsolete + in-place index patch" per ogni `add_edge`:
  1. Mutare prima `node->neighborgs[type][end]` in RAM. La funzione di persistenza opera sullo stato finale.
  2. Leggere `NodeIndex` corrente + `RelationNodeList` corrente per identificare le regioni che diventeranno orfane (la RelationNodeList stessa in `nodes.dat`, più i chunk per ciascuna relazione in `edges.dat`).
  3. Loggare le regioni orfane su `graph_io.log` come placeholder. **La freelist persistente non era implementata** — `FreeRecord` e `MetaRecord.free_count` esistevano come POD/campo ma non c'erano `freelist.dat` né i percorsi di read/write. Lo spazio orfanato restava byte morti. *(Aggiornamento 2026-06-03: la freelist esiste ora come bin segregati per size sotto `db/freelist/` e `FreeRecord` è stata rimossa — vedi [Freelist a bin segregati](#2026-06-03--freelist-a-bin-segregati-per-dimensione-esatta--cancellazione-nodo). `update_node_edges` però continua a solo loggare le regioni orfane: non le spinge ancora sui bin, quindi il leak su `add_edge` resta.)*
  4. Appendere a `nodes.dat` la nuova `RelationNodeList` POD (16 byte: `type_count` + `batch_size`) e poi, per ogni relazione, appendere il chunk di edge in `edges.dat` (fresh contiguous block) seguito dalla entry della tail in `nodes.dat`. Niente edge "vecchio" viene riusato.
  5. Patcharare in-place `NodeIndex.relation_offset` in `nodes.idx`. È l'**unica scrittura in-place** del sistema: tutti gli altri file restano append-only. `nodes.idx` resta a record fissi da 25 byte, quindi `seekp(node_id * sizeof(NodeIndex) + offsetof(NodeIndex, relation_offset))` è ben definito; aperto con `std::ios::binary | std::ios::in | std::ios::out` (non `app`, perché su Windows `seekp` in app mode viene ignorato).
- **Alternative considerate:**
  - *Linked-list di edge*: aggiungere `uint64_t next_offset` a `Edge`, la `RelationNodeList` salva solo `head_offset` e niente `edge_count`. Add è O(1), no write amplification. Costo: cambia il formato di `Edge` (32 → 40 byte), e la lettura diventa una chain-walk. Scartata per non rompere il formato `Edge` e per restare nella tradizione "chunk contigui per `(node, relation)`".
  - *Pre-allocazione di slot a capacità fissa per `(node, relation)`*: semplice ma spreca spazio e fa esplodere il file primario se la fanout è skewed. Scartata.
  - *In-place rewrite della `RelationNodeList` originale*: impossibile senza padding fisso o senza spostare il resto del file in coda. La coda è di lunghezza variabile, quindi padding sarebbe sprecato per la maggior parte dei nodi.
- **Conseguenze:**
  - `nodes.idx` smette di essere **append-only**. La documentazione di [Append-only data files](#2026-05-26--append-only-data-files-truncated-meta) va letta tenendo conto di questa eccezione.
  - Ogni `add_edge` produce **regioni orfane** in `nodes.dat` (vecchia `RelationNodeList`) e `edges.dat` (vecchi chunk di edge). Senza freelist, il DB cresce monotonicamente. Per uno smoke test è trascurabile; per un workload reale è un leak che va chiuso prima di poter parlare di "produzione".
  - Aggiunto il campo `batch_size` a `RelationNodeList` (POD da 8 → 16 byte). **Schema-break del formato on-disk**: qualunque `db/` pre-esistente non è più leggibile e va cancellato. Vedi [API change](api_changes.md#2026-05-30--relationnodelist-aggiunto-il-campo-batch_size).
  - L'ordine RAM-first/persist-second sacrifica una proprietà transazionale (se la persistenza fallisce, lo stato RAM è davanti al disco), ma allinea il "input della funzione di persistenza" con lo stato di verità che si vuole scrivere. Il processo tipicamente muore sull'eccezione, e un restart rilegge lo stato vecchio dal disco — niente inconsistenza permanente.
  - Atomicità del passo 5: scrittura non-fsync di 8 byte allineati. In pratica improbabile che si rompa, ma non formalmente garantito. Out of scope per ora.
- **Riferimenti:** `graph_core/io/graph_io.h:62` (declaration), `graph_core/io/graph_io.cpp:247` (impl), `graph_core/graph.cpp:53` (callsite), `graph_core/struct/pod_struct.h:78` (`RelationNodeList` con `batch_size`), `graph_core/odt/node_odt.cpp:27` (`node_to_relation_list` calcola `batch_size`). Bug correlato: [BUG-001](known_bugs.md#2026-05-26--bug-001-add_edge-non-persiste-su-disco).

---

### 2026-05-26 — Storage sidecar JSON per nodi COMPLEX

- **Stato:** active (path di scrittura presente ma non ancora compilabile — vedi [BUG-009..BUG-014](known_bugs.md))
- **Contesto:** L'entry precedente ([Introduzione tag NodeType::COMPLEX](#2026-05-26--introduzione-tag-nodetypecomplex--complexrecord-wip)) aveva lasciato aperta la domanda di *dove* mettere gli attributi JSON di un nodo COMPLEX: inline come blob length-prefixed in `nodes.dat`, oppure su file separato? Mettere JSON di lunghezza arbitraria in `nodes.dat` significherebbe gonfiare il file primario con dati che nella pratica vengono letti raramente (sono attributi di record), e rende meno utile l'append-only di lunghezza prevedibile.
- **Decisione:** Spostare gli attributi JSON in **file sidecar** dedicati sotto `db/attributes/`. L'header on-disk `ComplexHeader` ora porta `(type_label_size, json_file_path_size)` — il secondo campo era `json_attributes_size`, rinominato (vedi [API change](api_changes.md#2026-05-26--complexheaderjson_attributes_size--json_file_path_size)). Subito dopo l'header in `nodes.dat` si scrivono i due blob raw: prima `type_label`, poi `json_file_path`. Il payload JSON vero e proprio vive in un file separato `db/attributes/{prog_number}_{type_label}.json`.
  Per generare nomi univoci viene introdotta una nuova POD `JsonMeta { uint64_t prog_number; }` persistita in `db/attributes/attributes_meta.dat`, con due funzioni dedicate (`write_json_attributes_meta`, `read_json_attributes_meta`). La lettura è lazy-init: se il file manca, viene creato con `prog_number = 0`.
  Le costanti di path sono concentrate in `graph_core/costants.h`: `JSON_ATTR_PATH = "../db/attributes/"`, `JSON_ATTR_META_PATH = "../db/attributes/attributes_meta.dat"`, più una `META_FILE_PATH` per simmetria con `meta.dat`.
- **Alternative considerate:**
  - *JSON inline in `nodes.dat`*: scartata — gonfia il file primario con dati di lunghezza molto variabile (alcuni record COMPLEX possono avere KB di attributi), peggiora la località degli `seek` per la lettura dei `NodeRecord` semplici, e rende difficile aggiornare il JSON senza riscrivere la coda del file.
  - *Un singolo `attributes.dat` flat con offset/length*: avrebbe risolto la frammentazione ma reintrodotto la necessità di un freelist quando i JSON cambiano dimensione. Rimandata.
  - *Tutti i JSON dentro `db/`, senza sottocartella*: scartata — la sottocartella `attributes/` separa logicamente i sidecar dai file di formato primario e permette di pulirla a parte se serve.
- **Conseguenze:**
  - Il "formato on-disk" di un nodo COMPLEX non è più contenuto in `nodes.dat` da solo: serve un secondo lettore per `db/attributes/{name}` per ricostruire l'oggetto in RAM.
  - `read_node` (`graph_core/io/graph_io.cpp:86`) deve aggiungere un `case NodeType::COMPLEX:` che (a) legge `ComplexHeader` + due `read_string`, (b) apre il file JSON al path letto, (c) costruisce un `Node<ComplexRecord>` con il JSON come `json_attributes`. **Non ancora implementato.**
  - `prog_number` va incrementato dopo ogni write di un nodo COMPLEX e riflushato via `write_json_attributes_meta` — il path di write attuale non lo fa (vedi [BUG-014](known_bugs.md)).
  - Il path del file JSON costruito da `complex_node_to_record` (`{prog}_{label}.json`) era divergente dal path che `write_complex` effettivamente apriva (`JSON_ATTR_PATH / type_label`): risolto il 2026-05-30 passando `json_file_path` come parametro condiviso tra le due funzioni — vedi [BUG-013](known_bugs.md).
  - Le costanti di path sono ancora **relative** al working directory del binario (lo stesso vincolo di `DB_PATH`: launch da `build/`).
- **Riferimenti:** `graph_core/struct/pod_struct.h:134` (ComplexHeader rinominato), `graph_core/struct/pod_struct.h:148` (JsonMeta), `graph_core/costants.h:9-11`, `graph_core/odt/node_odt.cpp:55` (complex_node_to_record), `graph_core/io/graph_io.cpp:33` (write_complex), `graph_core/io/graph_io.cpp:136-181` (write/read json meta).

---

### 2026-05-26 — Introduzione tag NodeType::COMPLEX + ComplexRecord (WIP)

- **Stato:** active (path di I/O ancora WIP)
- **Contesto:** Il modello attuale ammette solo payload POD trivialmente copiabili (`int`, `float`, `double`, `char`, `bool`). Si vogliono però rappresentare nodi "tipo record" che a runtime portano un'etichetta di tipo (`"Athlete"`, `"Item"`, `"Company"`, ecc.) e un insieme variabile di attributi. La closed-set dei POD non basta: serve un tag generico che ammetta payload a forma libera.
- **Decisione:** Introdotto il tag `NodeType::COMPLEX = 255` in `pod_struct.h` (deliberatamente in fondo allo spazio uint8_t per lasciare 5..254 alle eventuali nuove primitive future). Affiancata la `ComplexHeader` POD (size del type label + size del JSON, seguita dai due blob raw) come header on-disk. Sul lato RAM è stato introdotto `ComplexRecord { std::string type_label; std::string json_attributes; }` in `domain_struct.h`, mappato a `NodeType::COMPLEX` via `node_type_of<ComplexRecord>` in `type_registry.h`. `ComplexRecord` **non è POD** — la separazione [POD vs Domain](#2026-05-26--separazione-pod-vs-domain-struct) è quindi preservata, ma la coppia (`ComplexHeader`, `ComplexRecord`) ha una traduzione D↔P non banale: il payload non si copia raw, va serializzato campo per campo (header POD + due `write_string`).
- **Alternative considerate:**
  - *Estendere `node_type_of` a `std::string`*: scartata — uno `std::string` da solo non porta semantica di "che tipo di record è questo", e mappare uno solo dei due campi del payload sarebbe arbitrario.
  - *Tipo wrapper completamente nuovo che eredita da `BaseNode`* (es. `ComplexNode : BaseNode`): scartata per ora — significherebbe rompere lo schema `Node<T> : BaseNode` con `T` come payload; si è preferito tenere lo schema e introdurre `ComplexRecord` come `T` non-POD, lasciando al path di I/O la responsabilità della serializzazione speciale.
  - *Solo `ComplexHeader` mappato a COMPLEX, senza ComplexRecord*: scartata — la mappatura `node_type_of<ComplexHeader>` sarebbe semanticamente sbagliata, perché `ComplexHeader` è la parte POD del record on-disk, non il record stesso (manca il payload). Esporlo come `T` confonderebbe il sito chiamante.
- **Conseguenze:**
  - `Graph::insert<ComplexRecord>(...)` non compilava: il path passava per `node_to_record` (`odt/node_odt.h:22`) che ha `static_assert(std::is_trivially_copyable_v<T>)`. Dal 2026-05-30 il problema è chiuso da due fix combinati: `if constexpr` in `write_node` che salta del tutto la chiamata a `node_to_record` per `T = ComplexRecord` (vedi [BUG-010](known_bugs.md)), e `if constexpr` in `Graph::insert` per evitare `std::to_string(data)` sul ramo COMPLEX (vedi [BUG-015](known_bugs.md)). `Graph::insert<ComplexRecord>` ora compila; resta aperto [BUG-014](known_bugs.md) come blocker funzionale per il round-trip end-to-end (sidecar JSON sovrascritto).
  - `read_node` (`graph_core/io/graph_io.cpp:49`) dovrà acquisire un `case NodeType::COMPLEX:` che istanzia `Node<ComplexRecord>` e legge `ComplexHeader` + due stringhe length-prefixed da `nodes.dat`.
  - Il formato on-disk per COMPLEX è di fatto **non ancora congelato** — il commit di oggi introduce solo il tag e i POD/struct di RAM. Il vincolo "non rompere il formato esistente" si applicherà a partire dalla prima scrittura reale di un nodo COMPLEX.
  - Il valore numerico `255` è ora un'occupazione permanente: ridefinirlo significa rompere il formato. Le prossime primitive useranno valori `5..254`.
- **Riferimenti:** `graph_core/struct/pod_struct.h:16` (enum NodeType), `graph_core/struct/pod_struct.h:134` (ComplexHeader), `graph_core/struct/domain_struct.h:33` (ComplexRecord), `graph_core/struct/type_registry.h:33` (specializzazione). Vedi anche [BUG-001..BUG-005](known_bugs.md) — il path I/O di COMPLEX dovrà essere progettato evitando di replicarli.

---

### 2026-05-26 — Separazione POD vs Domain struct

- **Stato:** active
- **Contesto:** Il grafo serve sia "in vivo" (in RAM, con pointer e mappe non lineari per la velocità) sia "in scatola" (su disco, con layout fisso noto byte-a-byte). Usare la stessa rappresentazione per entrambi i mondi vorrebbe dire pagare un costo in uno dei due: pointer su disco non hanno senso, e struct packed con offset in RAM rallentano gli accessi.
- **Decisione:** Tenere due gerarchie parallele. La famiglia "domain" (`BaseNode`, `Node<T>`) ottimizzata per la RAM (mappe `unordered_map`, pointer ai vicini); la famiglia "POD" (`NodeRecord<T>`, `NodeIndex`, `RelationNodeList`, `Edge`, `MetaRecord`) ottimizzata per il disco (`#pragma pack(push, 1)`, tipi `uint64_t` fissi). Un layer dedicato (`odt/`) traduce tra le due.
- **Alternative considerate:**
  - *Unica struct serializzabile*: scartata perché obbligherebbe a usare offset/handle anche in RAM o a sopportare pack(1) per il dominio.
  - *Serialization library (es. flatbuffers, capnproto)*: scartata per non aggiungere dipendenze esterne in un progetto didattico/esplorativo.
- **Conseguenze:**
  - Più codice da scrivere (uno strato ODT esplicito).
  - Ogni nuova feature deve toccare in genere tre file: domain, POD, ODT.
  - I cambiamenti al POD rompono il formato su disco — vincolo permanente, non recuperabile senza migration.
- **Riferimenti:** `graph_core/struct/domain_struct.h`, `graph_core/struct/pod_struct.h`, `graph_core/odt/`.

---

### 2026-05-26 — Type-erased BaseNode + Node\<T\>

- **Stato:** active
- **Contesto:** Si vogliono nodi con payload di tipo diverso (`int`, `float`, `double`, `char`, `bool`) nello stesso grafo, gestiti dallo stesso `Graph` senza istanziare un `Graph<T>` per tipo.
- **Decisione:** `BaseNode` è una struct *non* templata che contiene solo l'adiacenza (`neighborgs`). `Node<T> : public BaseNode` aggiunge il payload `T data`. Il grafo possiede `unordered_map<int, BaseNode*>` — quindi puntatori al tipo erased; il vero tipo è recuperabile lato disco via `NodeType` in `NodeIndex` (vedi [type_registry](../modules/graph_core.md#templateclass-t-struct-node_type_of-structtype_registryh)).
- **Alternative considerate:**
  - *`std::variant<int, float, ...>` come payload*: forzerebbe la closed-set in un posto solo (più ergonomico) ma rende le copie più costose e richiede `std::visit` ovunque.
  - *`void* data` + `enum NodeType`*: rinuncia ai vantaggi della type-safety C++ e obbliga a `reinterpret_cast`.
- **Conseguenze:**
  - `BaseNode` deve avere destructor virtuale (presente).
  - L'ownership è centralizzata in `Graph::nodes` → `Graph::~Graph` itera e `delete`a tutto.
  - Quando si fa `read_node`, la dispatch via `switch(type_id)` è il punto in cui si "rinasce" il tipo concreto: ogni nuovo `T` richiede di aggiornare il `switch` in `graph_io.cpp:read_node`, l'enum `NodeType`, e una specializzazione di `node_type_of`.
- **Riferimenti:** `graph_core/struct/domain_struct.h:15`, `graph_core/struct/type_registry.h:8`, `graph_core/io/graph_io.cpp:49`.

---

### 2026-05-26 — Policy-based traversal (BFS/DFS)

- **Stato:** active
- **Contesto:** BFS e DFS differiscono solo nel tipo di frontiera (`queue` vs `stack`) e nel modo di prelevare il prossimo nodo (`front+pop` vs `top+pop`). Duplicare il codice di traversal era inutile.
- **Decisione:** Una sola funzione template `Graph::traverse<Policy, NodeFn, EdgeFn>` definisce l'algoritmo. Le `Policy` sono struct con `using Frontier = ...` + `push/pop/empty` statici. `bfs` e `dfs` sono wrapper che selezionano la policy.
- **Alternative considerate:**
  - *Funzione virtuale o `std::function`*: introduce indirezione a runtime in un hot loop.
  - *Due funzioni distinte*: duplicazione, divergenza nel tempo.
- **Conseguenze:**
  - La policy è scelta a compile-time → zero overhead.
  - Aggiungere una nuova strategia (es. priority queue) richiede solo una nuova struct policy + un wrapper.
  - L'utente non è esposto al tipo `Policy`: usa `bfs()`/`dfs()`.
- **Riferimenti:** `graph_core/struct/functions_policies.h:12`, `graph_core/graph.h:76`.

---

### 2026-05-26 — Append-only data files, truncated meta

- **Stato:** active
- **Contesto:** Insert frequenti, mai (per ora) cancellazioni o riscritture in-place. Si vuole massima semplicità di scrittura e poter sapere a colpo d'occhio dove finirà il prossimo record.
- **Decisione:**
  - `nodes.dat`, `nodes.idx`, `edges.dat` → aperti con `std::ios::binary | std::ios::app`. Le scritture sono sempre in fondo. Gli offset si ottengono via `tellp()` prima della `write`.
  - `meta.dat` → aperto con `std::ios::binary | std::ios::trunc` ad ogni `write_meta`: viene riscritto per intero (24 byte all'epoca della decisione; 48 byte dal 2026-06-02, vedi [API change MetaRecord](api_changes.md#2026-06-02--metarecord-aggiunti-i-campi-edge_count-next_edge_id-free_edge_count)). Costo trascurabile.
- **Alternative considerate:**
  - *In-place updates*: richiederebbe un freelist e records di lunghezza variabile gestiti con cura. Rimandato.
- **Conseguenze:**
  - Il file system cresce monotono finché non si introducono cancellazioni. `MetaRecord.free_count` e `FreeRecord` erano definiti per quel futuro. *(Aggiornamento 2026-06-03: arrivata `Graph::delete_node` + freelist a bin segregati; `FreeRecord` rimossa e sostituita da tre POD free-offset — vedi [Freelist a bin segregati](#2026-06-03--freelist-a-bin-segregati-per-dimensione-esatta--cancellazione-nodo).)*
  - Nessuna ricostruzione/compaction è prevista al momento.
  - I bug di scrittura non sono recuperabili: un record corrotto resta nel file.
- **Riferimenti:** `graph_core/io/graph_io.h:46`, `graph_core/io/graph_io.cpp:74`.

---

### 2026-05-26 — Single-open append su nodes.dat

- **Stato:** active
- **Contesto:** `write_node` deve scrivere prima un `NodeRecord<T>` e poi una `RelationNodeList` (con coda variabile) nello stesso file `nodes.dat`, e annotare entrambi gli offset nel `NodeIndex`. Su Windows, riaprire un file in modalità `app` e chiamare `tellp()` *prima* di scrivere ritorna 0, non l'offset reale di fine file — il valore corretto si vede solo dopo la prima `write`.
- **Decisione:** `write_node` apre `nodes.dat` **una sola volta** all'inizio, fa `seekp(0, std::ios::end)`, e usa lo stesso `ofstream` per entrambe le scritture (record + relation list). `write_relation_node_list` riceve l'`ofstream` come parametro invece di riaprirlo.
- **Alternative considerate:**
  - *Riaprire `nodes.dat` due volte*: scartata per il bug `tellp() == 0` sopra descritto.
  - *Aprire senza `app` e seek esplicito*: equivalente, ma più verboso.
- **Conseguenze:**
  - `write_relation_node_list` ha una firma che richiede `std::ofstream &out` — non può essere chiamato a sé stante senza un caller che apra il file.
  - L'ordine di scrittura dei due blocchi è vincolato a quello del codice attuale.
- **Riferimenti:** `graph_core/io/graph_io.h:60`, `graph_core/io/graph_io.h:94` (commento esplicito sul bug Windows).

---

### 2026-05-26 — POD packed e fragilità ABI

- **Stato:** active
- **Contesto:** I record su disco devono avere offset di campo prevedibili byte-a-byte, indipendentemente dall'ABI del compilatore.
- **Decisione:** Tutti i POD persistiti (`NodeIndex`, `NodeRecord<T>`, `RelationNodeList`, `Edge`, `MetaRecord`, e dal 2026-06-03 `NodeFreeOffset` / `RelationNodeListFreeOffset` / `BatchOfEdgesFreeOffset`; `FreeRecord` rimossa) sono avvolti in `#pragma pack(push, 1)` / `#pragma pack(pop)`. Le scritture passano per `write_pod` che `static_assert`a `is_trivially_copyable_v<T>` e scrive `sizeof(T)` byte raw.
- **Alternative considerate:**
  - *Padding "naturale" + commenti*: fragile; ogni compilatore poteva scegliere padding diverso.
  - *Serializzazione campo-per-campo*: più sicura ma verbosa, e cancellerebbe il vantaggio della copia raw.
- **Conseguenze:**
  - Format dipendente dall'endianness del processore (al momento solo little-endian → ok su x86-64/Windows). Migrare ad arch big-endian rompe i file esistenti.
  - Modificare anche un solo campo di un POD invalida tutti i `db/` esistenti. Non c'è un version field.
  - L'`enum class NodeType : uint8_t` espone valori numerici stabili → cambiare un enumerator è un format break.
- **Riferimenti:** `graph_core/struct/pod_struct.h`, `graph_core/io/io_utils.h:19`.

---

### 2026-05-26 — Hash table standalone in C (non linkata)

- **Stato:** active (orfana — non usata dal target `graph`)
- **Contesto:** Il primo prototipo del progetto era in C (`node_n_pointers.c`) e usava array di pointer per i vicini. L'autore aveva annotato l'intenzione di passare a una hash table per accesso O(1) ai vicini (vedi commento in `node_n_pointers.c:13-26`). La hash table è stata scritta (`data_tructures/map_hash_table.{c,h}`) ma il progetto è poi migrato a C++ e ha adottato `std::unordered_map`.
- **Decisione:** Conservare il modulo `data_tructures/` come riferimento storico e potenziale base per futuri esperimenti. Non collegarlo al `CMakeLists.txt` corrente.
- **Alternative considerate:**
  - *Eliminare il modulo*: scartata — è codice utile come materiale di studio o per progetti futuri.
  - *Linkarlo come libreria opzionale*: scartata — non ha senso fintanto che nessun consumatore lo usa, e il modulo ha ancora bug noti (vedi [BUG-006](known_bugs.md), [BUG-007](known_bugs.md), [BUG-008](known_bugs.md)).
- **Conseguenze:**
  - Il modulo non viene compilato → eventuali errori di compilazione passano inosservati. Va riverificato prima di un eventuale reintegro.
  - Il nome della cartella resta `data_tructures` (refuso storico) per non rompere link e file path già scritti altrove.
- **Riferimenti:** `data_tructures/map_hash_table.h`, `data_tructures/map_hash_table.c`, `node_n_pointers.c:13`.
