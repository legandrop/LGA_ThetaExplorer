# ThetaExplorer Docs

Este directorio centraliza la documentacion funcional y tecnica del proyecto.

## Documentos

- `architecture-current-state.md`
  Describe como funciona hoy la app, que objetos maneja y donde estan los limites actuales para soportar HDR agrupado.

- `hdr-grouping-proposal.md`
  Propone la estrategia recomendada para detectar secuencias HDR, representarlas en la UI y operar sobre sets completos para descargar y borrar.

- `implementation-plan.md`
  Baja la propuesta a fases concretas de implementacion para evitar mezclar refactor, heuristica y cambios de UX en un mismo paso.

  Explica la herramienta de exportacion de catalogo real desde la app para calibrar la deteccion HDR sobre datos de la Z1 y no sobre supuestos.

- `logging.md`
  Documenta el sistema de logging central, las categorias, las variables de entorno y el criterio de uso.

- `hdr-grouping-observed-patterns.md`
  Resume el patron real observado en el catalogo exportado de la Z1 y la regla de agrupacion implementada a partir de esa evidencia.

- `deploy.md`
  Como `deploy.sh` compila Release, firma, arma el ZIP y el DMG y publica el release en GitHub, y que necesita el sitio para mostrar Descargar.

- `ui.md`
  Paleta y QSS con variables, fuente Inter embebida, iconos SVG, toolbar, estados vacios, carteles de confirmacion, avisos flotantes y Help.

- `session-and-download-behavior.md`
  Documenta persistencia de ventana y settings, carpeta de descarga, badges Saved/Partial, los carteles de descargar y borrar, los avisos y la estructura final de subcarpetas por grupo.

## Criterio

Antes de tocar comportamiento visible conviene mantener actualizada esta carpeta. En este proyecto es especialmente importante porque la app necesita separar:

- catalogo plano que entrega la camara
- assets agrupados que consume la UI
- acciones por archivo real versus acciones por set HDR

Sin esa distincion, la UI termina mezclando conceptos y aparecen errores de seleccion, preview y borrado.
