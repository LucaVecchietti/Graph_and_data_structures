# Legacy log — API changes

> Changes to function signatures, struct layouts, or observable behavior on the public surface (or what will become it). Each entry pairs the previous form with the new one and explains the reason.

| Campo | Valore |
|---|---|
| Tipo | legacy-api |
| Lingua | en |
| Ultimo aggiornamento | 2026-05-26 |
| Commit di riferimento | 9e8b589 |
| Mirror | — |

---

## Indice

(nessuna entry registrata ancora)

---

> Questo file è pronto a ricevere entries la prima volta che cambia una firma o una struct osservabile. Il template per ogni entry è in [STANDARD.md §3.2](../STANDARD.md#32-entry-in-api_changesmd).
>
> Nota: il progetto non ha ancora consumatori esterni, quindi "API pubblica" qui significa: classe `Graph`, POD persistiti su disco (`pod_struct.h`), funzioni esposte nei header pubblici (`io/graph_io.h`, `io/io_utils.h`, `odt/*.h`, `struct/*.h`).
>
> Le modifiche ai POD persistiti sono particolarmente sensibili perché rompono il formato su disco — andranno tracciate qui anche se prive di consumatori esterni.
