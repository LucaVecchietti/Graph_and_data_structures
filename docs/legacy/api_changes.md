# Legacy log — API changes

> Changes to function signatures, struct layouts, or observable behavior on the public surface (or what will become it). Each entry pairs the previous form with the new one and explains the reason.

| Campo | Valore |
|---|---|
| Tipo | legacy-api |
| Lingua | en |
| Ultimo aggiornamento | 2026-06-03 |
| Commit di riferimento | ca567e4 |
| Mirror | — |

---

## Indice

- [2026-06-03 — `FreeRecord` rimossa, sostituita da tre POD free-offset](#2026-06-03--freerecord-rimossa-sostituita-da-tre-pod-free-offset)
- [2026-06-03 — Nuova `Graph::delete_node(int)`](#2026-06-03--nuova-graphdelete_nodeint)
- [2026-06-03 — `Graph::insert<T>`: aggiunto il reuse path via freelist](#2026-06-03--graphinsertt-aggiunto-il-reuse-path-via-freelist)
- [2026-06-03 — Nuove funzioni I/O freelist + `delete_node_from_disk`](#2026-06-03--nuove-funzioni-io-freelist--delete_node_from_disk)
- [2026-06-03 — Nuovo `node_record_payload_size` + TU `type_registry.cpp`](#2026-06-03--nuovo-node_record_payload_size--tu-type_registrycpp)
- [2026-06-02 — `MetaRecord`: aggiunti i campi `edge_count`, `next_edge_id`, `free_edge_count`](#2026-06-02--metarecord-aggiunti-i-campi-edge_count-next_edge_id-free_edge_count)
- [2026-06-02 — `BaseNode::neighborgs`: da `pair<int, BaseNode*>` a `EdgeRef`](#2026-06-02--basenodeneighborgs-da-pairint-basenode-a-edgeref)
- [2026-05-30 — `Graph::add_edge`: persistenza su disco via `update_node_edges`](#2026-05-30--graphadd_edge-persistenza-su-disco-via-update_node_edges)
- [2026-05-30 — Nuova funzione `update_node_edges`](#2026-05-30--nuova-funzione-update_node_edges)
- [2026-05-30 — `RelationNodeList`: aggiunto il campo `batch_size`](#2026-05-30--relationnodelist-aggiunto-il-campo-batch_size)
- [2026-05-26 — `write_node<T>` switch su `NodeType` per ramo COMPLEX](#2026-05-26--write_nodet-switch-su-nodetype-per-ramo-complex)
- [2026-05-26 — Nuova firma `complex_node_to_record`](#2026-05-26--nuova-firma-complex_node_to_record)
- [2026-05-26 — Nuove funzioni `write_complex` / `write_json_attributes_meta` / `read_json_attributes_meta`](#2026-05-26--nuove-funzioni-write_complex--write_json_attributes_meta--read_json_attributes_meta)
- [2026-05-26 — Nuove costanti di path in `costants.h`](#2026-05-26--nuove-costanti-di-path-in-costantsh)
- [2026-05-26 — `ComplexHeader.json_attributes_size` → `json_file_path_size`](#2026-05-26--complexheaderjson_attributes_size--json_file_path_size)

---

> Nota: il progetto non ha ancora consumatori esterni, quindi "API pubblica" qui significa: classe `Graph`, POD persistiti su disco (`pod_struct.h`), funzioni esposte nei header pubblici (`io/graph_io.h`, `io/io_utils.h`, `odt/*.h`, `struct/*.h`).
>
> Le modifiche ai POD persistiti sono particolarmente sensibili perché rompono il formato su disco — andranno tracciate qui anche se prive di consumatori esterni.

---

### 2026-06-03 — `FreeRecord` rimossa, sostituita da tre POD free-offset

- **Motivazione:** `FreeRecord` portava solo un `uint64_t offset` — niente size, niente id: insufficiente per riusare in sicurezza un buco di dimensione nota o per riassegnare un id. La nuova freelist a bin segregati per size (vedi [decisione](design_decisions.md#2026-06-03--freelist-a-bin-segregati-per-dimensione-esatta--cancellazione-nodo)) richiede `(idx?, offset, size)` per regione.
- **Before:**
  ```cpp
  #pragma pack(push, 1)
  struct FreeRecord {
      uint64_t offset; // free offset in nodes.dat
  };
  #pragma pack(pop)
  ```
- **After:**
  ```cpp
  // pod_struct.h — tre POD distinte, una per tipo di regione reclamabile
  #pragma pack(push, 1)
  struct NodeFreeOffset {            // regione NodeRecord in nodes.dat + slot id riusabile in nodes.idx
      uint64_t idx;    // id nodo riusabile
      uint64_t offset; // inizio del NodeRecord orfanato
      uint64_t size;   // byte della regione libera
  };
  struct RelationNodeListFreeOffset { // regione RelationNodeList (header + coda) in nodes.dat
      uint64_t offset;
      uint64_t size;   // = sizeof(RelationNodeList) + batch_size
  };
  struct BatchOfEdgesFreeOffset {     // chunk contiguo di Edge in edges.dat
      uint64_t idx;    // id del primo arco del batch (id riusabile di partenza)
      uint64_t offset;
      uint64_t size;   // = edge_count * sizeof(Edge)
  };
  #pragma pack(pop)
  ```
- **Note di migrazione:** `FreeRecord` non era mai stata scritta su disco, quindi nessun `db/` esistente la contiene: la rimozione **non** è uno schema-break dei file dati. Le tre nuove POD sono scritte/lette dai template `write_free_offset` / `pop_free_offset` nei bin `db/freelist/<prefix>_<size>.dat`. `MetaRecord.free_count` / `free_edge_count` restano riservati (ancora mai scritti se non `0`): `delete_node_from_disk` non aggiorna i contatori (vedi [BUG-016](known_bugs.md#2026-06-03--bug-016-delete_node-prototipo-non-aggiorna-idx-contatori-meta-archi-entranti-complex)).
- **Riferimenti:** commit `ca567e4`. `graph_core/struct/pod_struct.h:182,196,209`.

---

### 2026-06-03 — Nuova `Graph::delete_node(int)`

- **Motivazione:** Prima non esisteva alcuna cancellazione di nodo. Insieme alla freelist, fornisce il lato "push" del reclamo dello spazio (vedi [decisione](design_decisions.md#2026-06-03--freelist-a-bin-segregati-per-dimensione-esatta--cancellazione-nodo)).
- **Before:** assente.
- **After:**
  ```cpp
  // graph.h:117
  void delete_node(int node_id);
  ```
  Comportamento: se il nodo non è in RAM ma `node_id < meta.next_id`, lo lazy-carica via `read_node` (stesso pattern di `add_edge`); altrimenti throw `std::out_of_range`. Logga il numero di archi uscenti che verranno persi, rimuove il nodo da `nodes` e fa `delete` del puntatore, poi chiama `delete_node_from_disk(node_id, meta)` per spingere le regioni del nodo (NodeRecord, RelationNodeList, ogni chunk di edge) sui bin della freelist.
- **Note di migrazione:** **Prototipo, non una cancellazione completa.** Limiti tracciati in [BUG-016](known_bugs.md#2026-06-03--bug-016-delete_node-prototipo-non-aggiorna-idx-contatori-meta-archi-entranti-complex): lo slot in `nodes.idx` non è tombstonato (l'entry punta ancora a byte morti), i contatori `meta` non sono aggiornati, gli archi **entranti** da altri nodi non sono rimossi (vicini dangling), il payload COMPLEX a size variabile non è gestito. Throw `std::runtime_error` se la rilettura del nodo da disco fallisce.
- **Riferimenti:** commit `6774330`, `ca567e4`. `graph_core/graph.h:117` (decl), `graph_core/graph.cpp:163` (impl).

---

### 2026-06-03 — `Graph::insert<T>`: aggiunto il reuse path via freelist

- **Motivazione:** Lato "pop" del reclamo: riusare uno slot/regione liberati da una `delete_node` precedente invece di appendere sempre in fondo.
- **Before:**
  ```cpp
  // sempre append: id fresco e regione nuova in coda
  nodes[meta.next_id] = newNode;
  write_node(*newNode, meta);
  meta.node_count++;
  meta.next_id++;
  ```
- **After:**
  ```cpp
  // reuse path: solo per primitivi a size fissa (mai COMPLEX)
  if constexpr (node_type_of_v<ValueType> != NodeType::COMPLEX) {
      uint64_t needed = node_record_payload_size(node_type_of_v<ValueType>);
      std::optional<NodeFreeOffset> slot = pop_free_offset<NodeFreeOffset>(freelist_bin_path("nodes", needed));
      if (slot) {
          node_id = slot->idx;                                 // ricicla l'id liberato
          nodes[node_id] = newNode;
          write_node_in_freed_slot(*newNode, node_id, slot->offset);
          meta.node_count++;                                   // next_id NON avanza
          reused = true;
      }
  }
  if (!reused) {                                               // append path invariato
      node_id = meta.next_id;
      nodes[node_id] = newNode;
      write_node(*newNode, meta);
      meta.node_count++;
      meta.next_id++;
  }
  ```
- **Note di migrazione:** La firma di `insert` è invariata. Cambia il comportamento osservabile: dopo una `delete_node`, una `insert` di un primitivo della **stessa size** riusa id e regione (il log riporta `(reused slot)`); `next_id` non avanza, `node_count` sì. Il `if constexpr` impedisce anche l'istanziazione di `write_node_in_freed_slot<ComplexRecord>` (fallirebbe lo `static_assert` di `node_to_record`). COMPLEX prende sempre l'append path. Il log su ramo COMPLEX usa `node_id` invece di `meta.next_id - 1`.
- **Riferimenti:** commit `6774330`, `ca567e4`. `graph_core/graph.h:43`.

---

### 2026-06-03 — Nuove funzioni I/O freelist + `delete_node_from_disk`

- **Motivazione:** Implementare push/pop sulla freelist a bin segregati per size e la cancellazione lato disco (vedi [decisione](design_decisions.md#2026-06-03--freelist-a-bin-segregati-per-dimensione-esatta--cancellazione-nodo)).
- **Before:** assenti.
- **After:**
  ```cpp
  // graph_io.h
  std::filesystem::path freelist_bin_path(const std::string &prefix, uint64_t size); // inline
  template <typename T> void               write_free_offset(const T &fo, const std::filesystem::path &p);
  template <typename T> std::optional<T>   pop_free_offset(const std::filesystem::path &p);
  template <typename T> void               write_node_in_freed_slot(const Node<T> &node, uint64_t node_id, uint64_t record_offset);
  void                                     delete_node_from_disk(uint64_t node_id, const MetaRecord &meta);
  ```
  Comportamento:
  - `freelist_bin_path` → `db/freelist/<prefix>_<size>.dat`.
  - `write_free_offset` → append di un record (push); crea `db/freelist/` al primo uso.
  - `pop_free_offset` → legge l'ultimo record e tronca il file di un record (pop LIFO, fit esatto); `std::nullopt` se il bin manca/è vuoto; throw se la `resize_file` fallisce.
  - `write_node_in_freed_slot` → scrive il `NodeRecord<T>` **in place** alla regione liberata e il `NodeIndex` **in place** allo slot id liberato in `nodes.idx`; la `RelationNodeList` (vuota per un nodo fresco) è comunque **appesa** a fine `nodes.dat`.
  - `delete_node_from_disk` → legge `NodeIndex` + `RelationNodeList` del nodo, calcola le regioni reclamabili e le spinge sui bin `nodes`/`rel`/`edges`. Per i chunk di edge legge il primo `Edge` per recuperare l'id di partenza del batch.
- **Note di migrazione:** `write_free_offset` / `pop_free_offset` sono template generici sui tre POD free-offset, dichiarati una sola volta (niente decl per-tipo). `delete_node_from_disk` prende `const MetaRecord &meta` ma oggi **non lo usa** (`(void)meta;`) — riservato per quando aggiornerà i contatori. `update_node_edges` **non** è stata cablata su `write_free_offset`: continua a solo loggare le regioni orfane (il suo TODO ora rimanda a questo path). Tutte throw `std::runtime_error` se un file non si apre.
- **Riferimenti:** commit `6774330`, `ca567e4`. `graph_core/io/graph_io.h:65,190,321,333,355`, `graph_core/io/graph_io.cpp:380`.

---

### 2026-06-03 — Nuovo `node_record_payload_size` + TU `type_registry.cpp`

- **Motivazione:** Servire la size on-disk del payload di un nodo dato il suo `NodeType`, per scegliere il bin della freelist (`freelist_bin_path("nodes", size)`) sia in `delete_node_from_disk` (push) sia in `Graph::insert` (pop).
- **Before:** `type_registry.h` era header-only (solo il template `node_type_of` / `node_type_of_v`); nessun `type_registry.cpp` nel build.
- **After:**
  ```cpp
  // type_registry.h (dichiarazione)
  size_t node_record_payload_size(NodeType type_id);
  // type_registry.cpp (nuova TU, aggiunta a CMakeLists.txt)
  //   INT→4, FLOAT→4, DOUBLE→8, CHAR→1, BOOL→1, COMPLEX→sizeof(ComplexHeader); default → throw
  ```
- **Note di migrazione:** La funzione è non-template e definita **out-of-line** in `type_registry.cpp` per avere una sola definizione nel programma (un corpo in header darebbe multiple-definition/ODR). `CMakeLists.txt` ora elenca `graph_core/struct/type_registry.cpp` tra i sorgenti di `graph_core`. **Limite noto:** per `NodeType::COMPLEX` ritorna solo `sizeof(ComplexHeader)`, non la size reale (header + due stringhe length-prefixed): chi deve reclamare un record COMPLEX deve calcolare le lunghezze a parte (vedi [BUG-016](known_bugs.md#2026-06-03--bug-016-delete_node-prototipo-non-aggiorna-idx-contatori-meta-archi-entranti-complex)). `NodeType::COMPLEX` non prende comunque mai il reuse path di `insert`.
- **Riferimenti:** commit `6774330`, `ca567e4`. `graph_core/struct/type_registry.h:51`, `graph_core/struct/type_registry.cpp`, `CMakeLists.txt:13`.

---

### 2026-06-02 — `MetaRecord`: aggiunti i campi `edge_count`, `next_edge_id`, `free_edge_count`

- **Motivazione:** Dotare il grafo di un bookkeeping degli archi simmetrico a quello dei nodi (`node_count` / `next_id` / `free_count`), propedeutico alla chiusura di [BUG-002](known_bugs.md#2026-05-26--bug-002-edgeid-non-globale-tra-nodi) (id arco globale) e alle future freelist degli offset di archi.
- **Before:**
  ```cpp
  // pod_struct.h — 24 byte
  struct MetaRecord {
      uint64_t next_id;
      uint64_t node_count;
      uint64_t free_count;
  };
  ```
- **After:**
  ```cpp
  // pod_struct.h — 48 byte
  struct MetaRecord {
      // Node bookkeeping
      uint64_t next_id;
      uint64_t node_count;
      uint64_t free_count;
      // Edge bookkeeping
      uint64_t edge_count;      // archi vivi
      uint64_t next_edge_id;    // prossimo id arco (sorgente di Edge.id)
      uint64_t free_edge_count; // freelist archi (riservato)
  };
  ```
- **Note di migrazione:** **Schema break sul formato su disco**: `meta.dat` passa da 24 a 48 byte. Ogni `db/meta.dat` prodotto prima del commit `309d3f9` va cancellato (`read_meta` farebbe una short-read). `init_meta` azzera tutti e sei i campi. `edge_count`/`next_edge_id` sono ora alimentati da `add_edge`; `free_edge_count` resta riservato (mai scritto se non `0`), come `free_count`.
- **Riferimenti:** commit `309d3f9`. `graph_core/struct/pod_struct.h:136`, `graph_core/graph.cpp:30` (`init_meta`).

---

### 2026-06-02 — `BaseNode::neighborgs`: da `pair<int, BaseNode*>` a `EdgeRef`

- **Motivazione:** Cablare `next_edge_id` come `Edge.id` ([BUG-002](known_bugs.md#2026-05-26--bug-002-edgeid-non-globale-tra-nodi)) richiede che l'id dell'arco sia ricordato in RAM, così che un arco riscritto da `update_node_edges` (che riscrive *tutti* gli archi del nodo) conservi il suo id. Vedi la [decisione](design_decisions.md#2026-06-02--id-arco-globale-sorgente-in-metarecordnext_edge_id-memorizzato-in-edgeref).
- **Before:**
  ```cpp
  // domain_struct.h — valore interno = (weight, neighbor_ptr)
  std::unordered_map<std::string,
      std::unordered_map<int, std::pair<int, BaseNode*>>> neighborgs;
  // accesso: edge.first (weight), edge.second (ptr)
  ```
- **After:**
  ```cpp
  // domain_struct.h
  struct EdgeRef { uint64_t id; int weight; BaseNode *neighbor; };
  std::unordered_map<std::string,
      std::unordered_map<int, EdgeRef>> neighborgs;
  // accesso: edge.id, edge.weight, edge.neighbor
  ```
- **Note di migrazione:** Tutti i siti che leggevano `.first`/`.second` sull'arco sono stati aggiornati: `Graph::traverse` (`graph.h`, `edge.weight`), i loop di scrittura `write_relation_node_list` (`graph_io.h:105`) e `update_node_edges` (`graph_io.cpp:344`) scrivono `ref.id`/`ref.weight`, il read path (`graph_io.h:236`) costruisce `EdgeRef{edge.id, weight, nullptr}`. La firma di `reconstruct_neighbors` (stub, [BUG-003](known_bugs.md#2026-05-26--bug-003-reconstruct_neighbors-non-implementata)) è stata allineata al nuovo tipo di ritorno. Nessun cambiamento al formato su disco (`Edge` POD invariato; cambia solo la *semantica* del campo `id`). Il typo `neihborgs` di `node_form_pod` ([BUG-004](known_bugs.md#2026-05-26--bug-004-typo-neihborgs-in-node_form_pod)) resta invariato (template non istanziato).
- **Riferimenti:** commit `309d3f9`. `graph_core/struct/domain_struct.h:24,38`, `graph_core/graph.cpp:137`, `graph_core/io/graph_io.h:105,236`, `graph_core/io/graph_io.cpp:344`, `graph_core/odt/node_odt.{h,cpp}`.

---

### 2026-05-30 — `Graph::add_edge`: persistenza su disco via `update_node_edges`

- **Motivazione:** Chiusura di [BUG-001](known_bugs.md#2026-05-26--bug-001-add_edge-non-persiste-su-disco). Prima del 2026-05-30 la funzione mutava solo l'adiacenza in RAM; gli archi erano persi al riavvio. Il nuovo flusso allinea lo stato persistito allo stato in RAM dopo ogni `add_edge`.
- **Before:**
  ```cpp
  void Graph::add_edge(int start, int end, std::string type, int weight)
  {
      // ... lazy load di start/end se non in RAM ...
      BaseNode *node = nodes[start];
      auto edge = std::pair<int, BaseNode*>(weight, nodes[end]);
      node->neighborgs[type][end] = edge;
      // No disk write.
  }
  ```
- **After:**
  ```cpp
  void Graph::add_edge(int start, int end, std::string type, int weight)
  {
      // ... lazy load di start/end se non in RAM ...
      BaseNode *node = nodes[start];
      auto edge = std::pair<int, BaseNode*>(weight, nodes[end]);
      // RAM-first: la persistenza legge node->neighborgs aggiornato.
      node->neighborgs[type][end] = edge;
      // Disk: rewrite della RelationNodeList + chunk edge + patch nodes.idx.
      update_node_edges(*node, meta, start);
  }
  ```
- **Note di migrazione:** Side effects on disk passano da **none** a **multipli**: append in `nodes.dat`, append in `edges.dat`, scrittura in-place in `nodes.idx`, log su `graph_io.log`. La firma resta invariata. Vedi anche la [design decision](design_decisions.md#2026-05-30--edge-persistence-append--obsolete--in-place-index-patch) per le conseguenze (regioni orfane senza freelist, `nodes.idx` non più append-only).
- **Riferimenti:** commit `621e356`, `d7ba798`. `graph_core/graph.cpp:53` (callsite), `graph_core/io/graph_io.cpp:247` (impl di `update_node_edges`).

---

### 2026-05-30 — Nuova funzione `update_node_edges`

- **Motivazione:** Implementare la persistenza del cambio di adiacenza per un nodo già scritto su disco, senza riscrivere l'intero `nodes.dat`. Necessaria per chiudere [BUG-001](known_bugs.md#2026-05-26--bug-001-add_edge-non-persiste-su-disco).
- **Before:** assente.
- **After:**
  ```cpp
  // graph_core/io/graph_io.h:62
  void update_node_edges(BaseNode &node, const MetaRecord &meta, uint64_t node_id);
  ```
  Comportamento: legge `NodeIndex[node_id]` e la `RelationNodeList` corrente, logga le regioni che diventeranno orfane (placeholder per la futura freelist), poi appende a `nodes.dat` la nuova `RelationNodeList` ricostruita da `node.neighborgs` e, per ogni relazione, un chunk fresh contiguo a `edges.dat`. Infine patcha in-place `NodeIndex.relation_offset` in `nodes.idx` con il nuovo offset.
- **Note di migrazione:** `node_id` è atteso essere l'id di un nodo già inserito (cioè con un'entry valida in `nodes.idx`). Il parametro `const MetaRecord &meta` è oggi inutilizzato (`(void)meta;` nel corpo) — riservato per quando la freelist persistente userà `meta.free_count`. Throw `std::runtime_error` se uno qualunque dei file non si apre. Si appoggia a `node_to_relation_list` per ottenere la POD `RelationNodeList` corretta (con `batch_size` valorizzato — vedi [API change correlato](#2026-05-30--relationnodelist-aggiunto-il-campo-batch_size)). L'unica chiamante attualmente è `Graph::add_edge`.
- **Riferimenti:** commit `621e356`. `graph_core/io/graph_io.h:62` (decl), `graph_core/io/graph_io.cpp:247` (impl), `graph_core/graph.cpp:53` (callsite).

---

### 2026-05-30 — `RelationNodeList`: aggiunto il campo `batch_size`

- **Motivazione:** Necessario per due motivi: (1) consentire al lettore di acquisire la coda di lunghezza variabile in un solo `read` invece di walkare le entry a runtime; (2) avere a portata di mano la dimensione totale della regione `RelationNodeList` quando va orfanata da `update_node_edges`, in modo da poterla registrare in futuro su una freelist persistente. Il design dietro è descritto in [Edge persistence](design_decisions.md#2026-05-30--edge-persistence-append--obsolete--in-place-index-patch).
- **Before:**
  ```c
  #pragma pack(push, 1)
  struct RelationNodeList
  {
      uint64_t type_count; // Number of relation types the node has
      // Variable-width tail follows: type_count entries
  };
  #pragma pack(pop)
  // sizeof(RelationNodeList) == 8
  ```
- **After:**
  ```c
  #pragma pack(push, 1)
  struct RelationNodeList
  {
      uint64_t type_count;  // Number of distinct relation types this node has.
      uint64_t batch_size;  // Size in bytes of the variable-width tail (NOT including
                            // the 16 bytes of the POD). Used for one-shot reads and
                            // for freelist sizing when the region is orphaned.
      // Variable-width tail follows: type_count entries
  };
  #pragma pack(pop)
  // sizeof(RelationNodeList) == 16
  ```
- **Note di migrazione:** **Schema-break del formato on-disk.** Qualunque `db/` pre-esistente non è più leggibile (la `RelationNodeList` POD è cresciuta da 8 a 16 byte, gli offset di tutto ciò che segue si spostano). Va cancellato il `db/` prima del primo run con questo formato — coerente con la linea di `CLAUDE.md`.
  - `node_to_relation_list` in `graph_core/odt/node_odt.cpp:27` è stata adeguata a calcolare e popolare `batch_size` come somma di `3 * sizeof(uint64_t) + name.size()` per ogni relazione. Tutti i percorsi di write (`write_node` per nuovi nodi, `update_node_edges` per nodi già scritti) ottengono la POD coerente passando per questo helper.
  - Convenzione: `batch_size` è la dimensione della **coda** soltanto, NON include i 16 byte del POD stesso. La dimensione totale on-disk della regione è quindi `sizeof(RelationNodeList) + batch_size`. Convenzione documentata anche nel block-comment del POD in `pod_struct.h:78`.
- **Riferimenti:** commit `621e356`, `d7ba798`. `graph_core/struct/pod_struct.h:78` (POD), `graph_core/odt/node_odt.cpp:27` (`node_to_relation_list`), `graph_core/io/graph_io.cpp:112` (`read_relation_node_list`).

---

### 2026-05-26 — `ComplexHeader.json_attributes_size` → `json_file_path_size`

- **Motivazione:** Adottata la decisione di non scrivere il JSON inline in `nodes.dat` ma in un file sidecar (vedi [Storage sidecar JSON per nodi COMPLEX](design_decisions.md#2026-05-26--storage-sidecar-json-per-nodi-complex)). L'header on-disk non porta più la dimensione del payload JSON: porta la dimensione del path al file sidecar.
- **Before:**
  ```c
  #pragma pack(push, 1)
  struct ComplexHeader
  {
      uint64_t type_label_size;       // Size of the type label string
      uint64_t json_attributes_size;  // Size of the JSON attributes string
      // Followed by [type_label][json_attributes]
  };
  #pragma pack(pop)
  ```
- **After:**
  ```c
  #pragma pack(push, 1)
  struct ComplexHeader
  {
      uint64_t type_label_size;       // Size of the type label string
      uint64_t json_file_path_size;   // Size of the JSON file path string
      // Followed by [type_label][json_file_path]
  };
  #pragma pack(pop)
  ```
- **Note di migrazione:** Nessun consumatore esistente sul campo (il path di scrittura COMPLEX non era mai stato attivato). Le dimensioni dei due campi `uint64_t` sono invariate quindi i 16 byte dell'header sono compatibili byte-a-byte; cambia solo la **semantica** del secondo campo. Eventuali `db/attributes/` o `nodes.dat` prodotti da snapshot WIP precedenti vanno cancellati.
- **Riferimenti:** commit `d001109`, `graph_core/struct/pod_struct.h:134`.

---

### 2026-05-26 — Nuove costanti di path in `costants.h`

- **Motivazione:** Centralizzare i path su disco usati dai sidecar JSON e dal `meta.dat`.
- **Before:**
  ```cpp
  constexpr uint8_t RELATION_TYPE_MAX_SIZE = 255;
  constexpr std::string_view DB_PATH = "../db";
  ```
- **After:**
  ```cpp
  constexpr uint8_t RELATION_TYPE_MAX_SIZE = 255;

  constexpr std::string_view DB_PATH = "../db";
  constexpr std::string_view META_FILE_PATH = "../db/meta.dat";

  constexpr std::string_view JSON_ATTR_META_PATH = "../db/attributes/attributes_meta.dat";
  constexpr std::string_view JSON_ATTR_PATH = "../db/attributes/";
  ```
- **Note di migrazione:** I path restano **relativi** al working directory del binario (stesso vincolo di `DB_PATH`). `META_FILE_PATH` non è ancora usato dal codice — l'I/O di `meta.dat` continua a comporre il path inline come `std::filesystem::path(DB_PATH) / "meta.dat"`. È introdotto per coerenza con `JSON_ATTR_META_PATH`.
- **Riferimenti:** commit `d001109`, `graph_core/costants.h:7-11`.

---

### 2026-05-26 — Nuove funzioni `write_complex` / `write_json_attributes_meta` / `read_json_attributes_meta`

- **Motivazione:** Implementare il flusso di scrittura COMPLEX deciso in [Storage sidecar JSON](design_decisions.md#2026-05-26--storage-sidecar-json-per-nodi-complex).
- **Before:** assenti.
- **After:**
  ```cpp
  // in graph_io.h
  void       write_complex(const ComplexRecord &complex_record, std::ofstream &out);
  void       write_json_attributes_meta(const JsonMeta &meta);
  JsonMeta   read_json_attributes_meta();
  ```
  Comportamento atteso:
  - `write_complex`: scrive `ComplexHeader` (a partire da `ComplexRecord`), poi due `write_string` (`type_label`, `json_attributes`), poi apre un file JSON sidecar e scrive il payload JSON al suo interno.
  - `write_json_attributes_meta` / `read_json_attributes_meta`: persistono / leggono la POD `JsonMeta` in `db/attributes/attributes_meta.dat`. `read` lazy-crea il file con `prog_number = 0` se non esiste.
- **Note di migrazione:** Storia dell'implementazione:
  - `write_complex` era **definita due volte** in `graph_io.cpp` (linee 33 e 60) → la duplicazione è stata risolta il 2026-05-30, vedi [BUG-009](known_bugs.md);
  - chiamava `write_pod(complex_record, out)` su un `ComplexRecord` non POD → risolto il 2026-05-30: `write_complex` ora costruisce internamente un `ComplexHeader` POD da scrivere, vedi [BUG-011](known_bugs.md);
  - il path del file sidecar era incoerente con quello scritto dentro `ComplexHeader` → risolto il 2026-05-30: la nuova firma di `write_complex` accetta `json_file_path` come parametro e lo usa sia per la `write_string` che per aprire il file, vedi [BUG-013](known_bugs.md);
  - `prog_number` non viene mai incrementato dal write path → ancora aperto, vedi [BUG-014](known_bugs.md).
  Firma di `write_complex` aggiornata al 2026-05-30 (vedi voce dedicata sotto). Le firme sopra restano comunque il contratto di riferimento.
- **Riferimenti:** commit `36152ba`, `dfcfcb1`, `326920c`. `graph_core/io/graph_io.h:24,45-46`, `graph_core/io/graph_io.cpp:34,172,190`.

---

### 2026-05-26 — Nuova firma `complex_node_to_record`

- **Motivazione:** Bridge ODT dominio→POD per i nodi COMPLEX. Restituisce l'header da scrivere e, via out-param, il path del sidecar JSON da utilizzare a valle.
- **Before:** assente.
- **After:**
  ```cpp
  NodeRecord<ComplexHeader> complex_node_to_record(
      const Node<ComplexRecord> &node,
      std::string &json_file_path);
  ```
  Comportamento atteso: legge `JsonMeta`, compone `json_file_path` da `prog_number` + `type_label`, costruisce `ComplexHeader { type_label_size, json_file_path_size }`, lo impacchetta in `NodeRecord<ComplexHeader>` e lo ritorna.
- **Note di migrazione:** Risolto il 2026-05-30: la composizione del path usa ora `std::to_string(meta_json.prog_number)` (vedi [BUG-011](known_bugs.md)). Ancora aperto: `write_json_attributes_meta(meta_json)` non viene chiamata per persistere il `prog_number` incrementato (vedi [BUG-014](known_bugs.md)).
- **Riferimenti:** commit `dfcfcb1`. `graph_core/odt/node_odt.h:37`, `graph_core/odt/node_odt.cpp:55`.

---

### 2026-05-26 — `write_node<T>` switch su `NodeType` per ramo COMPLEX

- **Motivazione:** Differenziare la scrittura del record per i tipi primitivi (POD diretti) da quella dei tipi `COMPLEX` (header + sidecar JSON), senza specializzare il template per ogni `T`.
- **Before:**
  ```cpp
  template <typename T>
  void write_node(const Node<T> &node, const MetaRecord &meta)
  {
      std::ofstream dat_out(...);
      dat_out.seekp(0, std::ios::end);
      NodeRecord<T> record = node_to_record(node);
      uint64_t record_offset = dat_out.tellp();
      write_pod(record, dat_out);
      uint64_t relation_offset = write_relation_node_list<T>(node, meta.next_id, dat_out);
      // ...write_node_index...
  }
  ```
- **After:**
  ```cpp
  template <typename T>
  void write_node(const Node<T> &node, const MetaRecord &meta)
  {
      std::ofstream dat_out(...);
      NodeRecord<T> record;
      uint64_t record_offset;

      switch (node_type_of_v<T>)
      {
          case NodeType::INT:
          case NodeType::FLOAT:
          case NodeType::CHAR:
          case NodeType::DOUBLE:
          case NodeType::BOOL:
              dat_out.seekp(0, std::ios::end);
              record = node_to_record(node);
              record_offset = dat_out.tellp();
              write_pod(record, dat_out);
              break;

          case NodeType::COMPLEX:
              record = complex_node_to_record(node);          // signature mismatch
              dat_out.seekp(0, std::ios::end);
              record_offset = dat_out.tellp();
              write_complex(record, Node<ComplexRecord> node, dat_out);  // not valid C++
              break;
      }
      uint64_t relation_offset = write_relation_node_list<T>(node, meta.next_id, dat_out);
      // ...write_node_index...
  }
  ```
- **Note di migrazione:** Il ramo `case NodeType::COMPLEX:` **non compila** (vedi [BUG-010](known_bugs.md)): firma di `complex_node_to_record` mancante dell'out-param `json_file_path`, e la chiamata `write_complex(record, Node<ComplexRecord> node, dat_out)` ha una *declaration* in posizione di argomento e un terzo parametro che la firma di `write_complex` non prevede. Il ramo primitivi funziona come prima. Il commit lascia il template istanziabile solo per i tipi primitivi (un `insert<ComplexRecord>` istanzierebbe il ramo COMPLEX e farebbe fallire la build).
- **Riferimenti:** commit `dfcfcb1`. `graph_core/io/graph_io.h:104-144`.
