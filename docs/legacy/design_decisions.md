# Legacy log — Design decisions

> Architectural decisions taken so far, the reasoning behind them, and the trade-offs accepted.

| Campo | Valore |
|---|---|
| Tipo | legacy-decisions |
| Lingua | en |
| Ultimo aggiornamento | 2026-05-26 |
| Commit di riferimento | b6e7304-dirty |
| Mirror | — |

---

## Indice

- [2026-05-26 — Introduzione tag NodeType::COMPLEX + ComplexRecord (WIP)](#2026-05-26--introduzione-tag-nodetypecomplex--complexrecord-wip)
- [2026-05-26 — Separazione POD vs Domain struct](#2026-05-26--separazione-pod-vs-domain-struct)
- [2026-05-26 — Type-erased BaseNode + Node\<T\>](#2026-05-26--type-erased-basenode--nodet)
- [2026-05-26 — Policy-based traversal (BFS/DFS)](#2026-05-26--policy-based-traversal-bfsdfs)
- [2026-05-26 — Append-only data files, truncated meta](#2026-05-26--append-only-data-files-truncated-meta)
- [2026-05-26 — Single-open append su nodes.dat](#2026-05-26--single-open-append-su-nodesdat)
- [2026-05-26 — POD packed e fragilità ABI](#2026-05-26--pod-packed-e-fragilità-abi)
- [2026-05-26 — Hash table standalone in C (non linkata)](#2026-05-26--hash-table-standalone-in-c-non-linkata)

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
  - `Graph::insert<ComplexRecord>(...)` **non compila al momento**: il path passa per `node_to_record` (`odt/node_odt.h:22`) che ha `static_assert(std::is_trivially_copyable_v<T>)`. Serve un percorso di write parallelo (es. `write_complex_node`) e una variante di `insert` (o un metodo dedicato `insert_complex`) prima di poter usare effettivamente il tag.
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
  - `meta.dat` → aperto con `std::ios::binary | std::ios::trunc` ad ogni `write_meta`: viene riscritto per intero (24 byte). Costo trascurabile.
- **Alternative considerate:**
  - *In-place updates*: richiederebbe un freelist e records di lunghezza variabile gestiti con cura. Rimandato.
- **Conseguenze:**
  - Il file system cresce monotono finché non si introducono cancellazioni. `MetaRecord.free_count` e `FreeRecord` sono già definiti per quel futuro.
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
- **Decisione:** Tutti i POD persistiti (`NodeIndex`, `NodeRecord<T>`, `RelationNodeList`, `Edge`, `MetaRecord`, `FreeRecord`) sono avvolti in `#pragma pack(push, 1)` / `#pragma pack(pop)`. Le scritture passano per `write_pod` che `static_assert`a `is_trivially_copyable_v<T>` e scrive `sizeof(T)` byte raw.
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
