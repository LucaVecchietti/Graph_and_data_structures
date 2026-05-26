# Legacy log — Known bugs

> Bugs that are currently open, have been fixed (with the fix recorded), or have been explicitly accepted as `wontfix`. New entries go at the top.

| Campo | Valore |
|---|---|
| Tipo | legacy-bugs |
| Lingua | en |
| Ultimo aggiornamento | 2026-05-26 |
| Commit di riferimento | 9e8b589 |
| Mirror | — |

---

## Indice

- [2026-05-26 — BUG-008: `hash_map_remove` dichiarata, non implementata](#2026-05-26--bug-008-hash_map_remove-dichiarata-non-implementata)
- [2026-05-26 — BUG-007: `HashTable.size` mai incrementato](#2026-05-26--bug-007-hashtablesize-mai-incrementato)
- [2026-05-26 — BUG-006: typo `hash_map_reash`](#2026-05-26--bug-006-typo-hash_map_reash)
- [2026-05-26 — BUG-005: logger globale duplicato in `graph_io.cpp`](#2026-05-26--bug-005-logger-globale-duplicato-in-graph_iocpp)
- [2026-05-26 — BUG-004: typo `neihborgs` in `node_form_pod`](#2026-05-26--bug-004-typo-neihborgs-in-node_form_pod)
- [2026-05-26 — BUG-003: `reconstruct_neighbors` non implementata](#2026-05-26--bug-003-reconstruct_neighbors-non-implementata)
- [2026-05-26 — BUG-002: `Edge.id` non globale tra nodi](#2026-05-26--bug-002-edgeid-non-globale-tra-nodi)
- [2026-05-26 — BUG-001: `add_edge` non persiste su disco](#2026-05-26--bug-001-add_edge-non-persiste-su-disco)

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
