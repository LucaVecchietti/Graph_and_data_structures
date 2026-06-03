# Legacy log — Known bugs

> Bugs that are currently open, have been fixed (with the fix recorded), or have been explicitly accepted as `wontfix`. New entries go at the top.

| Campo | Valore |
|---|---|
| Tipo | legacy-bugs |
| Lingua | en |
| Ultimo aggiornamento | 2026-06-03 |
| Commit di riferimento | ca567e4 |
| Mirror | — |

---

## Indice

- [2026-06-03 — BUG-016: `delete_node` prototipo non aggiorna idx, contatori meta, archi entranti, COMPLEX](#2026-06-03--bug-016-delete_node-prototipo-non-aggiorna-idx-contatori-meta-archi-entranti-complex)
- [2026-05-30 — BUG-015: `Graph::insert` chiama `std::to_string` su `newNode->data`, incompatibile con `ComplexRecord`](#2026-05-30--bug-015-graphinsert-chiama-stdto_string-su-newnode-data-incompatibile-con-complexrecord)
- [2026-05-26 — BUG-014: `prog_number` mai incrementato/persistito dopo write COMPLEX](#2026-05-26--bug-014-prog_number-mai-incrementatopersistito-dopo-write-complex)
- [2026-05-26 — BUG-013: path del file JSON sidecar incoerente tra `complex_node_to_record` e `write_complex`](#2026-05-26--bug-013-path-del-file-json-sidecar-incoerente-tra-complex_node_to_record-e-write_complex)
- [2026-05-26 — BUG-012: logger globale duplicato in `node_odt.cpp`](#2026-05-26--bug-012-logger-globale-duplicato-in-node_odtcpp)
- [2026-05-26 — BUG-011: `complex_node_to_record` concatena `uint64_t + const char*`](#2026-05-26--bug-011-complex_node_to_record-concatena-uint64_t--const-char)
- [2026-05-26 — BUG-010: ramo `case NodeType::COMPLEX:` di `write_node` non compila](#2026-05-26--bug-010-ramo-case-nodetypecomplex-di-write_node-non-compila)
- [2026-05-26 — BUG-009: `write_complex` definita due volte in `graph_io.cpp`](#2026-05-26--bug-009-write_complex-definita-due-volte-in-graph_iocpp)
- [2026-05-26 — BUG-008: `hash_map_remove` dichiarata, non implementata](#2026-05-26--bug-008-hash_map_remove-dichiarata-non-implementata)
- [2026-05-26 — BUG-007: `HashTable.size` mai incrementato](#2026-05-26--bug-007-hashtablesize-mai-incrementato)
- [2026-05-26 — BUG-006: typo `hash_map_reash`](#2026-05-26--bug-006-typo-hash_map_reash)
- [2026-05-26 — BUG-005: logger globale duplicato in `graph_io.cpp`](#2026-05-26--bug-005-logger-globale-duplicato-in-graph_iocpp)
- [2026-05-26 — BUG-004: typo `neihborgs` in `node_form_pod`](#2026-05-26--bug-004-typo-neihborgs-in-node_form_pod)
- [2026-05-26 — BUG-003: `reconstruct_neighbors` non implementata](#2026-05-26--bug-003-reconstruct_neighbors-non-implementata)
- [2026-05-26 — BUG-002: `Edge.id` non globale tra nodi](#2026-05-26--bug-002-edgeid-non-globale-tra-nodi)
- [2026-05-26 — BUG-001: `add_edge` non persiste su disco](#2026-05-26--bug-001-add_edge-non-persiste-su-disco)

---

### 2026-06-03 — BUG-016: `delete_node` prototipo non aggiorna idx, contatori meta, archi entranti, COMPLEX

- **Stato:** open
- **Sintomo:** `Graph::delete_node` + `delete_node_from_disk` (`graph_core/graph.cpp:163`, `graph_core/io/graph_io.cpp:380`) liberano lo spazio del nodo sulla freelist, ma la cancellazione è parziale:
  1. lo slot del nodo in `nodes.idx` non è tombstonato: l'entry resta e punta a byte ormai morti. Una futura `read_node(node_id)` rileggerebbe spazzatura (o un record già riscritto da un reuse) finché lo slot non viene riusato via `pop_free_offset`;
  2. i contatori `meta` non sono aggiornati (`node_count` non decrementa, `free_count` / `free_edge_count` non incrementano) — per questo `delete_node_from_disk` prende ancora `meta` per `const &` e non chiama `write_meta`;
  3. gli archi **entranti** da altri nodi che puntano a `node_id` non vengono rimossi: restano in `nodes.dat`/`edges.dat` come vicini dangling (la cancellazione tocca solo l'adiacenza **uscente** del nodo);
  4. il payload **COMPLEX** a size variabile non è gestito: `node_record_payload_size` per COMPLEX ritorna solo `sizeof(ComplexHeader)`, quindi la regione spinta sul bin `nodes` sarebbe sotto-dimensionata e il file sidecar JSON non viene rimosso.
- **Root cause:** Implementazione volutamente a step: il commit introduce il lato push del reclamo (freelist) e la rimozione RAM, lasciando integrazione meta / tombstoning idx / cleanup archi entranti / COMPLEX a un passo successivo. I TODO sono annotati in coda a `Graph::delete_node` e a `delete_node_from_disk`.
- **Fix:** n/a (open). Step previsti: tombstonare/azzerare lo slot `nodes.idx` (o un flag in `NodeIndex.type_id`), aggiornare `meta` (`node_count--`, `free_count`/`free_edge_count++`) e `write_meta`, scansionare/rimuovere gli archi entranti, e calcolare la size reale del record COMPLEX (header + lunghezze stringhe) prima di spingerlo sul bin + cancellare il sidecar.
- **Regression guard:** nessuno (nessuna test suite). La Phase 3 di `main.cpp` esercita solo il caso felice (delete di un `int` + reuse dello slot via `insert`), confronto visuale su `graph.log` / `db/freelist/`.

---

### 2026-05-26 — BUG-009: `write_complex` definita due volte in `graph_io.cpp`

- **Stato:** fixed (2026-05-30)
- **Sintomo:** Build error `redefinition of 'write_complex'` (o equivalente `multiple definition` in fase di link) appena il file `graph_io.cpp` viene compilato.
- **Root cause:** `graph_core/io/graph_io.cpp` conteneva **due definizioni** della stessa funzione: una "vera" a riga 33 che scrive l'header, le due stringhe e il file sidecar; una stub vuota a riga 60 che faceva solo `return;`. La doc-comment a riga 56 era un placeholder di un secondo overload che non è mai diventato tale.
- **Fix (2026-05-30):** rimossa la stub e la sua doc-comment in `graph_core/io/graph_io.cpp`. La definizione "vera" precedente resta in piedi — soffre ancora di altri problemi semantici tracciati separatamente come [BUG-011](#2026-05-26--bug-011-complex_node_to_record-concatena-uint64_t--const-char) (`write_pod` su un tipo non POD) e [BUG-013](#2026-05-26--bug-013-path-del-file-json-sidecar-incoerente-tra-complex_node_to_record-e-write_complex) (path sidecar incoerente), ma il file ora ha una sola definizione e linka.
- **Regression guard:** la build pulita di `graph_core` (Ninja, no opt-in flags) è il test.

---

### 2026-05-26 — BUG-010: ramo `case NodeType::COMPLEX:` di `write_node` non compila

- **Stato:** fixed (2026-05-30)
- **Sintomo:** Istanziare il template `write_node<T>` per un `T` che mappava su `NodeType::COMPLEX` (oggi solo `ComplexRecord`) produceva errori di compilazione. Finché nessun chiamante istanziava il ramo, la build sopravviveva — `main.cpp` usa solo `int`, quindi non si notava.
- **Root cause:** `graph_core/io/graph_io.h` (ramo COMPLEX) conteneva tre errori:
  1. `record = complex_node_to_record(node);` — la firma reale è `complex_node_to_record(const Node<ComplexRecord>&, std::string &json_file_path)` e richiede l'out-param. Inoltre `record` era di tipo `NodeRecord<T>` (parametro template) mentre `complex_node_to_record` ritorna `NodeRecord<ComplexHeader>`: assegnazione invalida per `T != ComplexHeader`.
  2. `write_complex(record, Node<ComplexRecord> node, dat_out);` — `Node<ComplexRecord> node` in posizione di argomento era una **dichiarazione**, non un'espressione: parse error.
  3. La firma di `write_complex` era `(const ComplexRecord &, std::ofstream &)`: la chiamata passava 3 argomenti e il primo era del tipo sbagliato.
- **Fix (2026-05-30):** sostituito il `switch (node_type_of_v<T>)` con `if constexpr (node_type_of_v<T> == NodeType::COMPLEX)` in `graph_core/io/graph_io.h:write_node`. Il ramo COMPLEX ora chiama `complex_node_to_record(node, json_file_path)` con la firma corretta e poi `write_complex(node.data, json_file_path, dat_out)`. Con `if constexpr` solo il ramo applicabile a `T` viene istanziato: il ramo primitivi non vede mai `ComplexRecord` e viceversa, eliminando le condizioni che richiedevano i `reinterpret_cast` prefigurati nel pseudo-codice originale. Stessa pattern usata in [`read_typed_node`](../modules/graph_core.md) per simmetria. La firma di `write_complex` è stata ampliata a `(const ComplexRecord&, const std::string &json_file_path, std::ofstream&)` — vedi [API change correlato](api_changes.md). Bloccanti correlati [BUG-011](#2026-05-26--bug-011-complex_node_to_record-concatena-uint64_t--const-char) e [BUG-013](#2026-05-26--bug-013-path-del-file-json-sidecar-incoerente-tra-complex_node_to_record-e-write_complex) chiusi nella stessa sweep perché inseparabili a compile-time.
- **Regression guard:** un `(void)&write_node<ComplexRecord>;` in un test farebbe emergere subito una futura rottura del ramo COMPLEX. Dal 2026-05-30, con [BUG-015](#2026-05-30--bug-015-graphinsert-chiama-stdto_string-su-newnode-data-incompatibile-con-complexrecord) chiuso, anche un round-trip `Graph::insert(ComplexRecord{...}) + read_node` è sufficiente (purché [BUG-014](#2026-05-26--bug-014-prog_number-mai-incrementatopersistito-dopo-write-complex) non lo renda flaky sul secondo nodo).

---

### 2026-05-26 — BUG-011: `complex_node_to_record` concatena `uint64_t + const char*`

- **Stato:** fixed (2026-05-30)
- **Sintomo:** Compile error sull'espressione `meta_json.prog_number + "_" + node.data.type_label + ".json"` in `node_odt.cpp:70`. L'operatore `+` con `uint64_t` e `const char*` non costruisce una stringa: veniva interpretato come aritmetica su puntatore. Inoltre la `write_complex` chiamava `write_pod(complex_record, out)` su un `ComplexRecord` non trivialmente copiabile — fallisce lo `static_assert` in `write_pod`.
- **Root cause:** Mancava `std::to_string` attorno a `prog_number`. In più la `write_complex` riusava `write_pod` con un tipo non POD per "scrivere l'header": operazione semanticamente sbagliata — l'header da scrivere è il `ComplexHeader`, non il `ComplexRecord` di dominio.
- **Fix (2026-05-30):**
  1. In `graph_core/odt/node_odt.cpp:complex_node_to_record` la costruzione del path passa ora per `std::to_string(meta_json.prog_number) + "_" + node.data.type_label + ".json"`.
  2. La nuova `write_complex(const ComplexRecord&, const std::string &json_file_path, std::ofstream&)` in `graph_core/io/graph_io.cpp` costruisce internamente un `ComplexHeader` da `record.type_label.size()` e `json_file_path.size()` e lo scrive via `write_pod<ComplexHeader>` — il tipo è POD, lo `static_assert` passa. Successivamente scrive le due stringhe length-prefixed (`type_label`, `json_file_path`) e il file sidecar JSON.
- **Regression guard:** istanziare `write_node<ComplexRecord>` è ora sufficiente a verificare entrambi i punti del fix.

---

### 2026-05-26 — BUG-012: logger globale duplicato in `node_odt.cpp`

- **Stato:** fixed (2026-05-30)
- **Sintomo:** `graph_core/odt/node_odt.cpp:6` introduceva `Logger logger = Logger("node_odt.log", LogLevel::DEBUG);` a livello di translation unit. Stessa famiglia di problemi del logger globale di `graph_io.cpp` ([BUG-005](#2026-05-26--bug-005-logger-globale-duplicato-in-graph_iocpp)): nome non incapsulato (`logger`), external linkage, **collisione di simbolo** con la definizione gemella in `graph_io.cpp` (entrambe le TU vengono linkate perché `node_to_relation_list` è chiamata da `write_node`). In aggiunta: file di log dedicato, output multi-thread potenzialmente interlacciato.
- **Root cause:** Il pattern del logger globale usato in `graph_io.cpp` è stato copiato qui invece di essere astratto. Senza `static` né anonymous namespace, entrambi i `logger` finivano nel global namespace con external linkage → multiple definition.
- **Fix (2026-05-30):** `logger` in `graph_core/odt/node_odt.cpp` è ora dentro un anonymous namespace, che gli dà internal linkage e lo isola alla TU. Stessa sweep ha chiuso anche [BUG-005](#2026-05-26--bug-005-logger-globale-duplicato-in-graph_iocpp) (stesso anti-pattern in `graph_io.cpp`): collision di simbolo eliminata su tutta la coppia. L'API d'uso (`logger.error(...)`) e i file di log dedicati (`node_odt.log` / `graph_io.log`) restano invariati. Le opzioni più strutturali listate nel menu originale (singleton, parametro `Logger&`, membro di `Graph`) restano valide per future iterazioni se servirà condividere stato (es. unico file di log, mutex condiviso).
- **Regression guard:** una build pulita del target `graph_core` che linki entrambe le TU senza errori `multiple definition of logger` è il test.

---

### 2026-05-26 — BUG-013: path del file JSON sidecar incoerente tra `complex_node_to_record` e `write_complex`

- **Stato:** fixed (2026-05-30)
- **Sintomo:** Il path del file sidecar **scritto dentro `ComplexHeader`** non coincideva con il path **usato per aprire il file**.
  - `complex_node_to_record` costruiva `json_file_path = {prog_number}_{type_label}.json`.
  - `write_complex` apriva `std::filesystem::path(JSON_ATTR_PATH) / complex_record.type_label` — solo il `type_label`, senza `prog_number` e senza estensione `.json`.
  La lettura del nodo COMPLEX non avrebbe mai trovato il file scritto.
- **Root cause:** I due lati del flusso (ODT che costruisce il path, I/O che lo usa per aprire il file) non condividevano la stessa stringa: il path veniva ricomputato in modo diverso nei due punti.
- **Fix (2026-05-30):** la nuova firma `write_complex(const ComplexRecord&, const std::string &json_file_path, std::ofstream&)` accetta esplicitamente la stringa `json_file_path` come parametro — la stessa che `complex_node_to_record` ha appena scritto nel `ComplexHeader`. `write_complex` la usa sia per la `write_string` su `nodes.dat` sia per aprire `JSON_ATTR_PATH / json_file_path`. Un unico valore attraversa il flusso: niente possibilità di divergenza. L'helper `complex_attr_path` suggerito nel pseudo-fix originale non è più necessario.
- **Regression guard:** un round-trip insert→read di un singolo nodo COMPLEX (compila e gira dal 2026-05-30 con [BUG-015](#2026-05-30--bug-015-graphinsert-chiama-stdto_string-su-newnode-data-incompatibile-con-complexrecord) chiuso; due nodi COMPLEX con lo stesso `type_label` restano problematici finché [BUG-014](#2026-05-26--bug-014-prog_number-mai-incrementatopersistito-dopo-write-complex) è aperto).

---

### 2026-05-26 — BUG-014: `prog_number` mai incrementato/persistito dopo write COMPLEX

- **Stato:** open
- **Sintomo:** Anche al netto degli altri bug del ramo COMPLEX: `complex_node_to_record` legge `prog_number` via `read_json_attributes_meta` ma non lo **incrementa** e non chiama mai `write_json_attributes_meta(meta_json)` per persistere il nuovo valore. Conseguenza: ogni nodo COMPLEX verrebbe assegnato allo stesso `prog_number` (zero, alla prima esecuzione), e il file sidecar `{0}_{type_label}.json` verrebbe sovrascritto per ogni record di quel tipo.
- **Root cause:** Il design del [Storage sidecar JSON](design_decisions.md#2026-05-26--storage-sidecar-json-per-nodi-complex) prevede `prog_number` come contatore monotonico per garantire nomi univoci, ma la mutazione non è stata implementata.
- **Fix:** n/a (open). Dopo aver costruito `json_file_path` in `complex_node_to_record` (o nel chiamante, a seconda di dove si decide debba stare l'effetto collaterale), incrementare `meta_json.prog_number` e chiamare `write_json_attributes_meta(meta_json)`. Decidere se l'incremento avviene prima o dopo (riserva del numero) — meglio prima, così se la write fallisce il numero resta "consumato" ma non si rischiano collisioni.
- **Regression guard:** un test che inserisce due nodi COMPLEX con lo stesso `type_label` e verifica che producano due file distinti in `db/attributes/`.

---

### 2026-05-30 — BUG-015: `Graph::insert` chiama `std::to_string` su `newNode->data`, incompatibile con `ComplexRecord`

- **Stato:** fixed (2026-05-30)
- **Sintomo:** `Graph::insert<ComplexRecord>(...)` non compilava. L'errore arrivava da `graph_core/graph.h:59` — `logger.info("... and value " + std::to_string(newNode->data))` — perché `std::to_string` non ha overload per `ComplexRecord`. Per i tipi primitivi (`int, float, double, char, bool`) il template compilava come prima.
- **Root cause:** Il log per costruzione includeva il valore del nodo via `std::to_string(data)`. Funziona per primitivi numerici/`char`/`bool`, ma il template `insert<T>` è ora istanziabile anche per tipi non-aritmetici (vedi `ComplexRecord` dopo la chiusura di [BUG-010](#2026-05-26--bug-010-ramo-case-nodetypecomplex-di-write_node-non-compila)) e in quei casi il log falliva a compile-time.
- **Fix (2026-05-30):** sostituita la singola `logger.info(...)` con un `if constexpr (node_type_of_v<ValueType> == NodeType::COMPLEX)`. Il ramo primitivi mantiene il log preesistente (`"... and value " + std::to_string(newNode->data)`). Il ramo COMPLEX logga invece `"of complex type \"<type_label>\""`, che è l'informazione più utile disponibile senza dumpare il JSON. Solo il ramo applicabile a `ValueType` viene istanziato, quindi `std::to_string` non viene mai cercato per `ComplexRecord`.
- **Regression guard:** istanziare `Graph::insert<ComplexRecord>` (anche solo `(void)&Graph::insert<ComplexRecord>;`) ora compila ed esercita il ramo COMPLEX. Una build pulita di una TU che includa `graph.h` e prenda quel puntatore-a-funzione-membro è sufficiente.

---

### 2026-05-26 — BUG-001: `add_edge` non persiste su disco

- **Stato:** fixed (2026-05-30)
- **Sintomo:** Gli archi aggiunti dopo l'`insert` di un nodo vivevano solo in RAM. Riavviando il programma e rileggendo i nodi via `read_node`, l'adiacenza ricaricata non li conteneva.
- **Root cause:** `Graph::add_edge` (`graph_core/graph.cpp:53`) mutava `node->neighborgs[type][end] = edge;` ma non chiamava nessuna funzione di scrittura su `nodes.dat`/`edges.dat`. La `RelationNodeList` di un nodo veniva scritta una sola volta da `write_node` al momento dell'`insert`, quando ancora non c'erano archi.
- **Fix (2026-05-30):** scelta la variante (b) del menu originale — introdotta una nuova funzione `update_node_edges(BaseNode &node, const MetaRecord &meta, uint64_t node_id)` in `graph_core/io/graph_io.h:62` / `graph_core/io/graph_io.cpp:247`. Chiamata da `Graph::add_edge` *dopo* la mutazione del `neighborgs` in RAM. Strategia "append + obsolete + in-place index patch": (1) legge `NodeIndex` + `RelationNodeList` correnti per individuare le regioni da orfanizzare; (2) logga le regioni orfane su `graph_io.log` (la freelist persistente è il prossimo step); (3) appende a `nodes.dat` la nuova `RelationNodeList` e a `edges.dat` un nuovo chunk contiguo per ogni relazione; (4) patcha in-place `NodeIndex.relation_offset` in `nodes.idx`. `node_to_relation_list` ora popola anche il nuovo campo `batch_size` (vedi [API change](api_changes.md)). Dettagli architetturali nella [design decision dedicata](design_decisions.md#2026-05-30--edge-persistence-append--obsolete--in-place-index-patch).
- **Regression guard:** lo smoke test in `main.cpp` (a partire dal commit `d7ba798`) esegue insert + add_edge in fase 1, distrugge `Graph`, lo ricrea, forza il lazy-load via `add_edge` su una relazione fresca, e ri-esegue BFS. Se le BFS pre/post-restart producono lo stesso output, BUG-001 resta chiuso. Limitazione nota: lo smoke test non assert-failsa automaticamente — è un confronto visuale del log.

---

### 2026-05-26 — BUG-002: `Edge.id` non globale tra nodi

- **Stato:** fixed (2026-06-02, commit `309d3f9`)
- **Sintomo:** Il campo `Edge.id` non era univoco. Due archi appartenenti a nodi diversi potevano avere lo stesso `id` (in pratica, il primo arco di ogni nodo aveva `id = 0`).
- **Root cause:** In `write_relation_node_list` (`graph_core/io/graph_io.h`) e nel loop di scrittura di `update_node_edges`, `edge_idx` era una variabile locale inizializzata a 0 ad ogni chiamata (cioè per ogni nodo). Veniva incrementata solo all'interno del nodo corrente.
- **Fix:** Scelta di design "id globale" (vedi [decisione](design_decisions.md#2026-06-02--id-arco-globale-sorgente-in-metarecordnext_edge_id-memorizzato-in-edgeref)). Aggiunto `next_edge_id` a `MetaRecord` (sorgente monotona, persistita). L'id vive ora nella struct di dominio `EdgeRef { id, weight, neighbor }` (`graph_core/struct/domain_struct.h:24`): `add_edge` (`graph_core/graph.cpp:57`) assegna `id = meta.next_edge_id` a un arco nuovo e incrementa `next_edge_id`/`edge_count`; un arco già esistente riusa il suo `id`. I loop di scrittura (`graph_core/io/graph_io.h:105`, `graph_core/io/graph_io.cpp:344`) scrivono `ref.id` invece di un contatore locale; il read path (`graph_core/io/graph_io.h:236`) ricostruisce `EdgeRef` con `edge.id` letto da disco, così l'id sopravvive alle riscritture complete del nodo.
- **Regression guard:** nessuno (nessuna test suite). Verificato manualmente via lo smoke test di `main.cpp`: dump di `edges.dat` con id `0..5` distinti e id preservati (`road`=0/1, `knows`=2) dopo reload + riscrittura in Phase 2.

---

### 2026-05-26 — BUG-003: `reconstruct_neighbors` non implementata

- **Stato:** open
- **Sintomo:** Chiamando `reconstruct_neighbors` (`graph_core/odt/node_odt.cpp:46`) si ottiene sempre una mappa vuota, indipendentemente dal contenuto della `RelationNodeList` passata.
- **Root cause:** La funzione contiene solo `return neighbors;` su una mappa appena costruita. Il corpo non è mai stato scritto.
- **Fix:** n/a (open). Va implementata leggendo le `RelationEntry` dalla `RelationNodeList` ricostruita (vedi `read_relation_node_list` in `graph_io.cpp:28` come riferimento per il formato) e, per ciascuna, aprendo `edges.dat` agli offset indicati per costruire la mappa interna. Nota: `read_typed_node` in `graph_io.h:120` fa già questa logica direttamente, senza passare per `reconstruct_neighbors`; quindi al momento la funzione è codice morto.
- **Regression guard:** nessuno.

---

### 2026-05-26 — BUG-004: typo `neihborgs` in `node_form_pod`

- **Stato:** open
- **Sintomo:** Il template `node_form_pod` (`graph_core/odt/node_odt.h:54`) non compila se istanziato: a riga 58 scrive `node.neihborgs = ...` mentre il campo si chiama `neighborgs`. Attualmente non si nota perché nessun chiamante istanzia il template.
- **Root cause:** Refuso, mai esercitato.
- **Fix:** n/a (open). Correggere il refuso quando si decide se la funzione serve davvero (è duplicata rispetto a `read_typed_node`, vedi [BUG-003](#2026-05-26--bug-003-reconstruct_neighbors-non-implementata)).
- **Regression guard:** istanziarlo almeno una volta — un test che includa `node_odt.h` e provi a chiamare `node_form_pod<int>(...)` farebbe emergere il typo a compile-time.

---

### 2026-05-26 — BUG-005: logger globale duplicato in `graph_io.cpp`

- **Stato:** fixed (2026-05-30)
- **Sintomo:** Il file `graph_io.cpp` dichiarava `Logger logger = Logger("graph_io.log", LogLevel::DEBUG);` come variabile globale a livello di translation unit. Quando `node_odt.cpp` ha replicato lo stesso pattern (vedi [BUG-012](#2026-05-26--bug-012-logger-globale-duplicato-in-node_odtcpp)), le due definizioni di `logger` con external linkage hanno iniziato a collidere a link-time. Sintomo aggiuntivo originario: due `Logger` distinti possono scrivere sullo stesso file da thread/handle diversi → output interlacciato e flush non sincronizzato.
- **Root cause:** Il logger di `graph_io` era creato come globale con external linkage invece che come membro di una classe / singleton / TU-local.
- **Fix (2026-05-30):** `logger` in `graph_core/io/graph_io.cpp` è ora dentro un anonymous namespace (internal linkage). Stesso fix applicato in tandem a [BUG-012](#2026-05-26--bug-012-logger-globale-duplicato-in-node_odtcpp). L'aspetto "multi-thread interleaving" non è risolto da questo fix — `Logger` non ha mutex interno — ma diventa rilevante solo se/quando il progetto introdurrà concorrenza, momento in cui la scelta naturale sarà la opzione (a)/(c) del menu originale (singleton o membro di `Graph`).
- **Regression guard:** la build pulita del target `graph_core` con `node_odt.cpp` + `graph_io.cpp` linkati insieme non emette più `multiple definition of logger`.

---

### 2026-05-26 — BUG-006: typo `hash_map_reash`

- **Stato:** open
- **Sintomo:** Se la load factor della hash table supera `0.75`, `hash_map_put` chiama `hash_map_rehash(hash_table)` (vedi `data_tructures/map_hash_table.c:37`) — ma la funzione è definita come `hash_map_reash` (manca una `h`) a riga 97. Il programma non compila linkando questo modulo se la chiamata viene istanziata, oppure (se l'ottimizzatore la elimina come dead code) il rehash semplicemente non avviene.
- **Root cause:** Refuso nel nome della funzione. Mai esercitato perché il modulo non è linkato (vedi anche [decisione](design_decisions.md#2026-05-26--hash-table-standalone-in-c-non-linkata)) e perché [BUG-007](#2026-05-26--bug-007-hashtablesize-mai-incrementato) impedisce comunque di raggiungere la soglia.
- **Fix:** n/a (open). Rinominare `hash_map_reash` → `hash_map_rehash` sia nella definizione che, se serve, in un prototype nel `.h`.
- **Regression guard:** nessuno. Una build del modulo come libreria con `-Werror` farebbe emergere l'`implicit declaration` del simbolo mancante.

---

### 2026-05-26 — BUG-007: `HashTable.size` mai incrementato

- **Stato:** open
- **Sintomo:** Il campo `HashTable.size` resta a `0` per tutta la vita della tabella, quindi `(float)size/num_buckets > 0.75` è sempre falso e il rehash non scatta mai, anche se la tabella diventa molto piena.
- **Root cause:** Né `hash_map_put` né `hash_map_remove` (quest'ultima comunque assente, vedi [BUG-008](#2026-05-26--bug-008-hash_map_remove-dichiarata-non-implementata)) toccano `hash_table->size`. È declared-but-unused.
- **Fix:** n/a (open). Incrementare `size` in `hash_map_put` *dopo* l'inserimento, decrementarlo in `hash_map_remove` quando implementata.
- **Regression guard:** nessuno.

---

### 2026-05-26 — BUG-008: `hash_map_remove` dichiarata, non implementata

- **Stato:** open
- **Sintomo:** `data_tructures/map_hash_table.h:23` dichiara `void hash_map_remove(HashTable*, const char*)`. Il `.c` non la definisce: un caller che la usasse darebbe undefined reference in linking.
- **Root cause:** Funzione mai scritta.
- **Fix:** n/a (open). Da implementare se/quando il modulo verrà riportato in build.
- **Regression guard:** nessuno.
