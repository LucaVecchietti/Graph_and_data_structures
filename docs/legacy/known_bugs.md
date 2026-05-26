# Legacy log — Known bugs

> Bugs that are currently open, have been fixed (with the fix recorded), or have been explicitly accepted as `wontfix`. New entries go at the top.

| Campo | Valore |
|---|---|
| Tipo | legacy-bugs |
| Lingua | en |
| Ultimo aggiornamento | 2026-05-26 |
| Commit di riferimento | 326920c |
| Mirror | — |

---

## Indice

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

### 2026-05-26 — BUG-009: `write_complex` definita due volte in `graph_io.cpp`

- **Stato:** open
- **Sintomo:** Build error `redefinition of 'write_complex'` (o equivalente `multiple definition` in fase di link) appena il file `graph_io.cpp` viene compilato.
- **Root cause:** `graph_core/io/graph_io.cpp` contiene **due definizioni** della stessa funzione: una "vera" a riga 33 che scrive l'header, le due stringhe e il file sidecar; una stub vuota a riga 60 che fa solo `return;`. La doc-comment a riga 56 sembra un placeholder di un secondo overload che non è mai diventato tale.
- **Fix:** n/a (open). Eliminare la stub a riga 60 (è la versione più recente in coda alla funzione vera) — la build attualmente fallisce, quindi non c'è il rischio di rompere comportamenti già esercitati. Verificare anche che la doc-comment "Write ComplexHeader struct on the disk" venga unita o eliminata.
- **Regression guard:** compilare `graph_core` come libreria singola con `-Werror=duplicate-decl-specifier` o equivalente — la build pulita è il test.

---

### 2026-05-26 — BUG-010: ramo `case NodeType::COMPLEX:` di `write_node` non compila

- **Stato:** open
- **Sintomo:** Istanziare il template `write_node<T>` per un `T` che mappa su `NodeType::COMPLEX` (oggi solo `ComplexRecord`) produce errori di compilazione. Finché nessun chiamante istanzia il ramo, la build sopravvive — `main.cpp` usa solo `int`, quindi non si nota.
- **Root cause:** `graph_core/io/graph_io.h:131-134` (ramo COMPLEX) contiene tre errori:
  1. `record = complex_node_to_record(node);` — la firma reale è `complex_node_to_record(const Node<ComplexRecord>&, std::string &json_file_path)` e richiede l'out-param. Inoltre `record` è di tipo `NodeRecord<T>` (parametro template) mentre `complex_node_to_record` ritorna `NodeRecord<ComplexHeader>`: assegnazione invalida per `T != ComplexHeader`.
  2. `write_complex(record, Node<ComplexRecord> node, dat_out);` — `Node<ComplexRecord> node` in posizione di argomento è una **dichiarazione**, non un'espressione: parse error.
  3. La firma di `write_complex` è `(const ComplexRecord &, std::ofstream &)`: la chiamata passa 3 argomenti e il primo è del tipo sbagliato.
- **Fix:** n/a (open). Pseudo-codice del fix:
  ```cpp
  case NodeType::COMPLEX: {
      std::string json_file_path;
      NodeRecord<ComplexHeader> hdr = complex_node_to_record(
          reinterpret_cast<const Node<ComplexRecord>&>(node), json_file_path);
      dat_out.seekp(0, std::ios::end);
      record_offset = dat_out.tellp();
      write_pod(hdr, dat_out);
      write_string(node.data.type_label,   dat_out);  // serve un Node<ComplexRecord>
      write_string(json_file_path,         dat_out);
      // scrittura del file sidecar al path json_file_path con node.data.json_attributes
      break;
  }
  ```
  Il `reinterpret_cast` è la spia che il design del template non è adatto a un payload non POD: probabilmente la soluzione più pulita è una **specializzazione esplicita** `template<> void write_node<ComplexRecord>(...)` accanto al template generico, rimuovendo il `switch`.
- **Regression guard:** un `static_assert` o una build con un test che istanzia esplicitamente `write_node<ComplexRecord>` (anche solo un `(void)&write_node<ComplexRecord>;`) farebbe emergere subito la mancata compilazione.

---

### 2026-05-26 — BUG-011: `complex_node_to_record` concatena `uint64_t + const char*`

- **Stato:** open
- **Sintomo:** Compile error sull'espressione `meta_json.prog_number + "_" + node.data.type_label + ".json"` in `node_odt.cpp:70`. L'operatore `+` con `uint64_t` e `const char*` non costruisce una stringa: viene interpretato come aritmetica su puntatore. Inoltre la `write_complex` chiama `write_pod(complex_record, out)` su un `ComplexRecord` non trivialmente copiabile — fallisce lo `static_assert` in `write_pod`.
- **Root cause:** Manca `std::to_string` attorno a `prog_number`. In più la `write_complex` riusa `write_pod` con un tipo non POD per "scrivere l'header": è un'operazione semanticamente sbagliata — l'header da scrivere è il `ComplexHeader` ritornato da `complex_node_to_record`, non il `ComplexRecord` di dominio.
- **Fix:** n/a (open). Versione corretta della costruzione del path:
  ```cpp
  json_file_path = std::to_string(meta_json.prog_number)
                   + "_" + node.data.type_label + ".json";
  ```
  In `write_complex` sostituire `write_pod(complex_record, out)` con la scrittura dell'header POD (`ComplexHeader`) — quindi rivedere la firma per ricevere `ComplexHeader` e `ComplexRecord` separatamente, oppure costruire l'header internamente.
- **Regression guard:** una build con il ramo COMPLEX istanziato (vedi [BUG-010](#2026-05-26--bug-010-ramo-case-nodetypecomplex-di-write_node-non-compila)) lo farebbe emergere.

---

### 2026-05-26 — BUG-012: logger globale duplicato in `node_odt.cpp`

- **Stato:** open
- **Sintomo:** `graph_core/odt/node_odt.cpp:6` introduce `Logger logger = Logger("node_odt.log", LogLevel::DEBUG);` a livello di translation unit. Stessa famiglia di problemi del logger globale di `graph_io.cpp` ([BUG-005](#2026-05-26--bug-005-logger-globale-duplicato-in-graph_iocpp)): nome non incapsulato (`logger`), file di log dedicato, possibile collision in TU che includono più .cpp con la stessa convenzione, output multi-thread potenzialmente interlacciato.
- **Root cause:** Il pattern del logger globale usato in `graph_io.cpp` è stato copiato qui invece di essere astratto.
- **Fix:** n/a (open). Stesso menu di [BUG-005](#2026-05-26--bug-005-logger-globale-duplicato-in-graph_iocpp). Se si sceglie il singleton, basta una refactor sola che copre entrambi.
- **Regression guard:** nessuno.

---

### 2026-05-26 — BUG-013: path del file JSON sidecar incoerente tra `complex_node_to_record` e `write_complex`

- **Stato:** open
- **Sintomo:** Il path del file sidecar **scritto dentro `ComplexHeader`** non coincide con il path **usato per aprire il file**.
  - `complex_node_to_record` costruisce `json_file_path = {prog_number}_{type_label}.json` (vedi [BUG-011](#2026-05-26--bug-011-complex_node_to_record-concatena-uint64_t--const-char), anche se non compila la *forma* del path è chiara).
  - `write_complex` (`graph_io.cpp:45`) apre `std::filesystem::path(JSON_ATTR_PATH) / complex_record.type_label` — solo il `type_label`, senza `prog_number` e senza estensione `.json`.
  Se entrambi venissero corretti per compilare, scriveremmo nel file `db/attributes/{type_label}` ma in `nodes.dat` registreremmo come path `{prog_number}_{type_label}.json` — la lettura del nodo COMPLEX non troverebbe mai il file.
- **Root cause:** I due lati del flusso (ODT che costruisce il path, I/O che lo usa per aprire il file) non condividono la stessa funzione di costruzione del path.
- **Fix:** n/a (open). Estrarre la costruzione del path in una funzione singola, ad esempio in `costants.h` o in un piccolo helper:
  ```cpp
  inline std::filesystem::path complex_attr_path(uint64_t prog_number, const std::string &type_label) {
      return std::filesystem::path(JSON_ATTR_PATH)
          / (std::to_string(prog_number) + "_" + type_label + ".json");
  }
  ```
  Sia `complex_node_to_record` (per il valore stored in `ComplexHeader`) sia `write_complex` (per aprire il file) devono usarla.
- **Regression guard:** un round-trip insert→read di un nodo COMPLEX.

---

### 2026-05-26 — BUG-014: `prog_number` mai incrementato/persistito dopo write COMPLEX

- **Stato:** open
- **Sintomo:** Anche al netto degli altri bug del ramo COMPLEX: `complex_node_to_record` legge `prog_number` via `read_json_attributes_meta` ma non lo **incrementa** e non chiama mai `write_json_attributes_meta(meta_json)` per persistere il nuovo valore. Conseguenza: ogni nodo COMPLEX verrebbe assegnato allo stesso `prog_number` (zero, alla prima esecuzione), e il file sidecar `{0}_{type_label}.json` verrebbe sovrascritto per ogni record di quel tipo.
- **Root cause:** Il design del [Storage sidecar JSON](design_decisions.md#2026-05-26--storage-sidecar-json-per-nodi-complex) prevede `prog_number` come contatore monotonico per garantire nomi univoci, ma la mutazione non è stata implementata.
- **Fix:** n/a (open). Dopo aver costruito `json_file_path` in `complex_node_to_record` (o nel chiamante, a seconda di dove si decide debba stare l'effetto collaterale), incrementare `meta_json.prog_number` e chiamare `write_json_attributes_meta(meta_json)`. Decidere se l'incremento avviene prima o dopo (riserva del numero) — meglio prima, così se la write fallisce il numero resta "consumato" ma non si rischiano collisioni.
- **Regression guard:** un test che inserisce due nodi COMPLEX con lo stesso `type_label` e verifica che producano due file distinti in `db/attributes/`.

---

### 2026-05-26 — BUG-001: `add_edge` non persiste su disco

- **Stato:** open
- **Sintomo:** Gli archi aggiunti dopo l'`insert` di un nodo vivono solo in RAM. Riavviando il programma e rileggendo i nodi via `read_node`, l'adiacenza ricaricata non li contiene.
- **Root cause:** `Graph::add_edge` (in `graph_core/graph.cpp:53`) modifica `node->neighborgs[type][end] = edge;` ma non chiama nessuna funzione di scrittura su `nodes.dat`/`edges.dat`. La `RelationNodeList` di un nodo viene scritta una sola volta da `write_node` al momento dell'`insert`, quando ancora non ci sono archi.
- **Fix:** n/a (open). Possibili strade: (a) rendere la coda della `RelationNodeList` di lunghezza variabile riscrivibile in append + meccanismo di "ultima versione vince" via offset più recente, (b) aggiungere un `flush_node(id)` che riscrive la node entry in coda e aggiorna `nodes.idx`, (c) introdurre un freelist e fare in-place updates (richiede record di dimensione fissa).
- **Regression guard:** nessuno. Un test minimo sarebbe: inserire 2 nodi, aggiungere un arco, distruggere `Graph`, ricrearlo, `read_node(0)` e verificare che `neighborgs["road"][1]` esista.

---

### 2026-05-26 — BUG-002: `Edge.id` non globale tra nodi

- **Stato:** open
- **Sintomo:** Il campo `Edge.id` non è univoco. Due archi appartenenti a nodi diversi possono avere lo stesso `id`.
- **Root cause:** In `write_relation_node_list` (`graph_core/io/graph_io.h:70`), `edge_idx` è una variabile locale inizializzata a 0 ad ogni chiamata (cioè per ogni nodo). Viene incrementata solo all'interno del nodo corrente.
- **Fix:** n/a (open). Se l'intento è davvero un id globale, va spostato nel `MetaRecord` (aggiungere `next_edge_id`) e persistito. Se l'id è invece pensato come "indice dell'arco dentro la sua lista di vicini per un tipo di relazione", allora il nome `id` è fuorviante e va rinominato (es. `local_idx`) — questa è una scelta di design da prendere prima del fix.
- **Regression guard:** nessuno.

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

- **Stato:** open
- **Sintomo:** Il file `graph_io.cpp` (riga 8) dichiara `Logger logger = Logger("graph_io.log", LogLevel::DEBUG);` come variabile globale a livello di translation unit. Se in futuro più TU includono `graph_io.cpp` o se si aggiunge un secondo file che fa lo stesso (per esempio `graph.cpp` ha il suo `Logger` membro), si possono avere `Logger` distinti che scrivono sullo stesso file da thread/handle diversi — output interlacciato e flush non sincronizzato.
- **Root cause:** Il logger di `graph_io` è creato come globale invece che come membro di una classe o singleton controllato.
- **Fix:** n/a (open). Soluzioni possibili: (a) singleton accessibile tramite header, (b) passare un `Logger&` come parametro alle funzioni di I/O, (c) usare il logger di `Graph` (richiede di passarlo dentro).
- **Regression guard:** nessuno. Test possibile: scrivere da due thread e ispezionare `graph_io.log` per linee tagliate.

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
