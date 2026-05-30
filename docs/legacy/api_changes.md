# Legacy log — API changes

> Changes to function signatures, struct layouts, or observable behavior on the public surface (or what will become it). Each entry pairs the previous form with the new one and explains the reason.

| Campo | Valore |
|---|---|
| Tipo | legacy-api |
| Lingua | en |
| Ultimo aggiornamento | 2026-05-30 |
| Commit di riferimento | d7ba798 |
| Mirror | — |

---

## Indice

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
