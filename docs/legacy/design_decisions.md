# Legacy log — Design decisions

> Architectural decisions taken so far, the reasoning behind them, and the trade-offs accepted.

| Campo | Valore |
|---|---|
| Tipo | legacy-decisions |
| Lingua | en |
| Ultimo aggiornamento | 2026-06-02 |
| Commit di riferimento | 309d3f9 |
| Mirror | — |

---

## Indice

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
  3. Loggare le regioni orfane su `graph_io.log` come placeholder. **La freelist persistente non è implementata** — `FreeRecord` e `MetaRecord.free_count` esistono come POD/campo ma non c'è ancora `freelist.dat` né i percorsi di read/write. Lo spazio orfanato resta come byte morti finché la freelist non arriverà.
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
