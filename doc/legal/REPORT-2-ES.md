# Código generado por IA y licenciamiento: Análisis para JNEXT

**Fecha:** 27 de marzo de 2026

**Preparado para:** Jorge Gonzalez Villalonga

**Asunto:** Estado legal actual del código generado por IA con respecto al licenciamiento, copyright y publicación bajo GPL-3.0 — adaptado al proyecto del emulador JNEXT ZX Spectrum Next

---

## Índice

1. [Comprensión de su situación y preocupaciones](#1-comprensión-de-su-situación-y-preocupaciones)
2. [Estado actual de la cuestión](#2-estado-actual-de-la-cuestión)
3. [Análisis detallado](#3-análisis-detallado)
4. [Respuestas a sus preguntas específicas](#4-respuestas-a-sus-preguntas-específicas)
5. [Conclusiones y recomendaciones](#5-conclusiones-y-recomendaciones)
6. [Preguntas adicionales de interés](#6-preguntas-adicionales-de-interés)
7. [Fuentes](#7-fuentes)

---

## 1. Comprensión de su situación y preocupaciones

Usted está desarrollando **JNEXT**, un emulador de ZX Spectrum Next escrito en C++17, casi en su totalidad a través de Claude Code, con una autoría directa de código mínima por su parte. El proyecto se almacena en un repositorio privado de GitHub e incorpora código existente con licencia GPL de otros repositorios.

### Especificaciones del proyecto

- **Naturaleza:** Un emulador software en tiempo real y multiplataforma del ordenador ZX Spectrum Next, orientado a una emulación híbrida con precisión de línea del diseño oficial del hardware FPGA
- **Base de código:** Más de 40 archivos fuente distribuidos entre la emulación del núcleo, pipeline de vídeo, motor de audio, subsistemas de E/S, interfaz gráfica Qt 6 y una ventana de depuración completa — una aplicación sustancial y arquitectónicamente compleja
- **Componentes de terceros:**
  - **FUSE Z80 core** (`third_party/fuse-z80/`) — incorporado del emulador FUSE 1.6.0, licenciado bajo **GPLv2-o-posterior**. Copyright (c) 1999-2016 Philip Kendall, Stuart Brady. Adaptado para JNEXT por Jorge Gonzalez Villalonga (2026). Este es el backend de emulación de la CPU Z80.
  - **spdlog** (`third_party/spdlog/`) — submódulo git, licenciado bajo **MIT**. Copyright (c) 2016-presente, Gabi Melman y contribuidores de spdlog. Utilizado para registro estructurado. Depende de la biblioteca **fmt** (también licenciada bajo MIT).
- **Licencia del proyecto:** GPLv3-o-posterior, según se declara en `LICENSE` y `README.md`
- **Titular del copyright:** Copyright (C) 2025-2026 Jorge Gonzalez Villalonga
- **Método de desarrollo:** Desarrollado casi en su totalidad a través de Claude Code (Anthropic), con el desarrollador proporcionando la dirección arquitectónica, las decisiones de diseño, la interpretación de la especificación de referencia VHDL, la revisión iterativa y las pruebas

### Sus preocupaciones principales

- **Titularidad del copyright**: Si puede reclamar copyright sobre código predominantemente generado por una herramienta de IA
- **Validez de la licencia**: Si puede aplicar de manera significativa la GPL-3.0 a código generado por IA
- **Exposición legal**: Si la publicación podría exponerle a demandas
- **Contaminación por datos de entrenamiento**: Si la salida del LLM podría contener código protegido por copyright de sus datos de entrenamiento
- **Consideraciones jurisdiccionales**: El impacto de estar ubicado en España, dentro de la UE
- **Mitigación de riesgos**: Si una distribución exclusivamente en binario reduciría la exposición

---

## 2. Estado actual de la cuestión

### 2.1 Posición de la Oficina de Copyright de EE.UU.

La Oficina de Copyright de EE.UU. ha establecido a través de múltiples decisiones y un informe formal (Parte 2, publicado el 29 de enero de 2025) que:

- **Se requiere autoría humana** para la protección del copyright. Las obras generadas únicamente por IA, sin una contribución creativa humana significativa, no son elegibles para la protección del copyright.
- **Las obras asistidas por IA pueden estar protegidas por copyright**, pero solo en la medida en que un autor humano haya contribuido con expresión creativa suficiente. La mera provisión de instrucciones (prompts) a una IA generalmente no se considera una contribución creativa suficiente.
- El uso de IA para asistir en la creación, o la inclusión de material generado por IA en una obra más amplia generada por humanos, **no impide automáticamente** la protección del copyright.
- La Oficina recomienda documentar la participación humana en el desarrollo asistido por IA.

### 2.2 Resoluciones judiciales clave

#### Thaler v. Perlmutter (Jurisprudencia consolidada a marzo de 2026)

El caso emblemático en EE.UU. sobre autoría de IA:

- **Tribunal de Distrito (agosto de 2023):** Resolvió que la IA no puede ser autora según la Ley de Copyright.
- **Tribunal de Apelaciones del Circuito de D.C. (marzo de 2025):** Confirmó la sentencia, declarando que la autoría humana es un "requisito fundamental." Es importante destacar que el tribunal aclaró: *"El requisito de autoría humana no prohíbe registrar el copyright de obras que fueron realizadas por o con la asistencia de inteligencia artificial."*
- **Tribunal Supremo de EE.UU. (2 de marzo de 2026):** Denegó el certiorari, convirtiendo esto efectivamente en jurisprudencia consolidada. La IA no puede ser autora, pero los humanos que utilizan IA pueden ser autores de las obras resultantes, dependiendo de su nivel de implicación creativa.

#### GEMA v. OpenAI (Munich, Alemania — noviembre de 2025)

Un caso emblemático europeo:

- El Tribunal Regional de Munich resolvió que los modelos GPT de OpenAI **infringieron el copyright** al memorizar y reproducir letras de canciones de forma casi literal.
- El tribunal sostuvo que **la memorización de datos de entrenamiento constituye reproducción** según el derecho de autor de la UE/Alemania.
- El tribunal rechazó la defensa de minería de textos y datos (TDM) de OpenAI.
- **Este caso está en apelación** y podría llegar al Tribunal de Justicia de la UE (TJUE).
- **Relevancia para JNEXT:** Esta sentencia establece (al menos en una jurisdicción de la UE) que si un modelo de IA reproduce contenido protegido por copyright de sus datos de entrenamiento, eso constituye infracción. Sin embargo, esta sentencia se refirió a la reproducción literal de letras de canciones — un escenario muy diferente al de generar código de emulador.

#### Doe v. GitHub / Demanda de GitHub Copilot (En curso)

- Presentada en noviembre de 2022 contra GitHub, Microsoft y OpenAI, alegando que Copilot reproduce código de fuente abierta sin la atribución adecuada.
- La mayoría de las pretensiones fueron desestimadas. Subsiste un conjunto reducido de pretensiones por incumplimiento contractual.
- **A principios de 2026, la demanda permanece sin resolver** y no ha producido una resolución definitiva sobre si el código generado por IA constituye una obra derivada de los datos de entrenamiento.

### 2.3 Marco legal de la UE

#### EU AI Act

- Las obligaciones de transparencia del Artículo 50 se aplicarán a partir de **agosto de 2026**.
- Requiere el etiquetado de contenido generado por IA: contenido totalmente generado por IA frente a contenido asistido por IA.
- La Agencia Española de Supervisión de la Inteligencia Artificial (AESIA) ha publicado 16 documentos de orientación.

#### Informe del Parlamento Europeo sobre Copyright e IA (2026)

- Propuso un licenciamiento mejorado y remuneración por el uso de datos de entrenamiento.
- Amplia aplicación territorial: el derecho de autor de la UE debería aplicarse a los modelos de IA comercializados en el mercado de la UE, independientemente de donde se realizara el entrenamiento.
- Aun no es ley, pero señala la dirección política de la UE.

#### Consideraciones específicas de España

- La legislación española de propiedad intelectual (Ley de Propiedad Intelectual, Real Decreto Legislativo 1/1996) requiere autoría humana, de forma coherente con la posición de la UE.
- AESIA está publicando activamente orientaciones sobre cumplimiento en materia de IA.
- No existe aun ninguna resolución judicial especifica en España sobre el copyright de código generado por IA.

### 2.4 Posiciones de la comunidad de código abierto

#### Free Software Foundation (FSF)

- Ha expresado su preocupación de que los LLM entrenados con código bajo licencia GPL podrían violar inadvertidamente las disposiciones de copyleft.
- No ha emitido ninguna declaración de política definitiva sobre si los desarrolladores pueden licenciar bajo GPL código generado por IA.

#### Política del kernel de Linux

La política más detallada de cualquier proyecto importante:

- **Los agentes de IA NO DEBEN añadir etiquetas Signed-off-by.** Solo los humanos certifican el DCO.
- Las contribuciones asistidas por IA deben incluir una etiqueta `Assisted-by:`.
- El desarrollador humano asume **toda la responsabilidad** de revisar el código generado por IA.
- Linus Torvalds ha declarado que las herramientas de IA deberían tratarse de forma no diferente a las ayudas de programación tradicionales.

#### Otros proyectos

- **QEMU y Gentoo** han prohibido las contribuciones generadas por IA.
- **Debian** lo debatió pero no alcanzó una decisión formal.
- No existe un consenso universal en la comunidad.

### 2.5 El incidente de "license laundering" de Chardet (marzo de 2026)

Un caso reciente muy relevante:

- El mantenedor de `chardet` utilizó Claude para reescribir toda la base de código y cambió la licencia de LGPL a MIT, alegando una "implementación de sala limpia" (clean room).
- La comunidad de código abierto criticó ampliamente esto como **"blanqueo de licencias" (license laundering)**.
- **Distinción clave con JNEXT:** El caso de chardet involucró el uso deliberado de IA para reescribir una base de código existente con el fin de eludir su licencia. JNEXT es una aplicación nueva y original — no una reescritura de un emulador existente para cambiar su licencia. El núcleo FUSE Z80 está debidamente incorporado y atribuido bajo su licencia original GPLv2-o-posterior, no reescrito para evadirla.

---

## 3. Análisis detallado

### 3.1 La paradoja del copyright para el código generado por IA

Existe una tensión fundamental:

1. **Para licenciar código bajo GPL-3.0, necesita ser titular del copyright sobre el.** La GPL es una licencia de copyright.
2. **El código generado por IA podría no ser susceptible de copyright** si la contribución del humano fue insuficiente.
3. **Si el código está en el dominio publico, no puede hacer cumplir efectivamente el copyleft** — cualquiera podría extraer esas porciones y utilizarlas sin las restricciones de la GPL.

Sin embargo, la realidad es matizada, y **su situación es particularmente solida:**

- Usted dirigió el desarrollo de un emulador complejo y arquitectónicamente sofisticado con múltiples subsistemas (CPU, memoria, pipeline de vídeo, motor de audio, despacho de E/S, módulos de periféricos, interfaz gráfica Qt, depurador).
- Tomó decisiones creativas y técnicas en todos los niveles: eligiendo el modelo de precisión (híbrido con precisión de línea), diseñando la arquitectura de módulos, seleccionando que señales VHDL modelar, decidiendo el orden de composición de capas, eligiendo estructuras de datos y algoritmos.
- Reviso, probo y refino iterativamente la salida — enviando repetidamente el código para su corrección contra la especificación VHDL autoritativa.
- Integró la salida de la IA con código existente protegido por copyright (FUSE Z80), lo que requirió un diseño cuidadoso de las interfaces.
- Mantuvo un plan de diseño detallado (`EMULATOR-DESIGN-PLAN.md`) que demuestra una dirección creativa sostenida a lo largo de 9 fases de desarrollo.
- El propio historial de git es evidencia de un desarrollo iterativo dirigido por un humano.

Este nivel de implicación va mucho más allá de "escribir un prompt y aceptar la salida." Es más cercano a un arquitecto que dirige un equipo de construcción — la visión creativa y las decisiones técnicas son suyas.

### 3.2 El riesgo de contaminación — Especifico de JNEXT

El argumento de "contaminación" tiene dos formas, pero ambas conllevan un **riesgo menor para un proyecto de emulador** que para software de propósito general:

**a) Riesgo de reproducción literal: BAJO**

- JNEXT implementa el comportamiento del hardware del ZX Spectrum Next basándose en archivos fuente VHDL — un dominio altamente especializado. El numero de implementaciones existentes en C++ de este hardware específico es muy reducido.
- El código no implementa algoritmos comunes ni funciones de biblioteca estándar, que es donde la coincidencia literal con datos de entrenamiento es más probable.
- La mayor parte de la base de código trata con emulación de registros de hardware, temporizado de raster, gestión de bancos de memoria y protocolos de periféricos — dominios donde la implementación está dictada por la especificación del hardware, no por elecciones creativas de programación.
- Los LLM modernos (incluido Claude) están entrenados para minimizar la memorización de datos de entrenamiento.

**b) Riesgo de similitud estructural: BAJO-MODERADO**

- Cierta similitud estructural con otros emuladores de ZX Spectrum (FUSE, ZEsarUX, CSpect) es inevitable porque todos implementan el mismo hardware. Esto es **scenes a faire** — la doctrina jurídica según la cual ciertas expresiones son tan habituales en un contexto determinado que no pueden protegerse por copyright. Al implementar un decodificador de instrucciones Z80 o el temporizado del ULA, solo hay un numero limitado de formas de hacerlo correctamente.
- El hecho de que la implementación este dirigida por una especificación VHDL autoritativa refuerza el argumento de scenes a faire: el código está dictado por el hardware, no por una elección creativa.

**c) El núcleo FUSE Z80 — gestionado correctamente**

- El núcleo FUSE Z80 está explícitamente incorporado en `third_party/fuse-z80/` con sus avisos de copyright originales intactos y su archivo de licencia GPLv2-o-posterior (`COPYING.GPL2`).
- No está disfrazado, reescrito ni relicenciado — está abiertamente atribuido y utilizado bajo su licencia original.
- GPLv2-o-posterior es compatible hacia adelante con GPLv3, por lo que incluirlo en un proyecto GPLv3 es jurídicamente correcto.

### 3.3 Compatibilidad de licencias en JNEXT

La pila de licencias del proyecto está limpia:

| Componente                 | Licencia       | Compatible con GPLv3?                                                |
|----------------------------|----------------|----------------------------------------------------------------------|
| Código fuente de JNEXT     | GPLv3-o-posterior | — (esta es la licencia del proyecto)                              |
| Núcleo FUSE Z80            | GPLv2-o-posterior | **Si** — la clausula "o posterior" permite su uso bajo GPLv3      |
| spdlog                     | MIT            | **Si** — MIT es permisiva, compatible con cualquier versión de GPL   |
| fmt (dependencia de spdlog)| MIT            | **Si**                                                               |
| SDL2 (dependencia externa) | zlib           | **Si** — zlib es permisiva                                          |
| Qt 6 (dependencia externa) | LGPLv3         | **Si** — LGPL es compatible con GPL cuando se enlaza dinámicamente   |

**No existen conflictos de licencias** en el árbol de dependencias del proyecto. La elección de la licencia GPLv3 es jurídicamente solida con respecto a todos los componentes de terceros.

### 3.4 La cuestión de "solo binario"

Distribuir solo binarios seria **contraproducente e impracticable** para JNEXT:

1. **La GPL-3.0 exige la disponibilidad del código fuente.** El núcleo FUSE Z80 es GPLv2-o-posterior, lo que requiere la distribución del código fuente. La propia licencia GPLv3 del proyecto también lo exige.
2. **Ocultar el código fuente no elimina el riesgo de contaminación.** La infracción (si la hubiera) existe en el momento de la generación, no de la publicación.
3. **El desarrollo dirigido por VHDL en realidad le beneficia.** Poder señalar la especificación autoritativa del hardware como fuente de sus decisiones de diseño es una fortaleza, no una vulnerabilidad.
4. **Socavaría la confianza de la comunidad.** Un proyecto de emulador se beneficia enormemente de la participación de la comunidad de código abierto.

---

## 4. Respuestas a sus preguntas específicas

### P1: Quien es titular del copyright del código generado por IA?

**Respuesta breve:** Usted tiene una pretensión solida sobre el copyright, dado su nivel de implicación creativa.

- **En EE.UU. (jurisprudencia consolidada tras Thaler):** La IA no puede ser autora. Si un humano aporta expresión creativa suficiente — decisiones de diseño, refinamiento iterativo, integración — el humano es el autor. Su dirección sostenida del desarrollo de JNEXT a lo largo de 9 fases, con decisiones arquitectónicas documentadas en `EMULATOR-DESIGN-PLAN.md` y verificadas contra especificaciones de hardware VHDL, constituye una implicación creativa sustancial.

- **En la UE/España:** Similar — se requiere una creación intelectual humana. Su papel como arquitecto y director del proyecto, tomando todas las decisiones significativas de diseño e implementación, respalda su pretensión de autoría.

- **Especifico de JNEXT:** El diseño del emulador está dirigido por una especificación autoritativa de hardware (fuente VHDL). Su contribución creativa incluye: elegir el modelo de precisión, diseñar la arquitectura de módulos, decidir como traducir las señales de hardware en abstracciones de software, diseñar la infraestructura de depuración y construir la interfaz gráfica en Qt. Estas no son interacciones triviales de prompt-y-aceptar — representan una dirección creativa sostenida de un proyecto de ingeniería complejo.

### P2: Cual es el estado actual de opinión entre desarrolladores, abogados y tribunales?

El panorama está fragmentado:

- **Tribunales:** Se requiere autoría humana (Thaler). Las obras asistidas por IA *pueden* estar protegidas por copyright (Oficina de Copyright de EE.UU.). La memorización por IA de datos de entrenamiento = infracción (GEMA v. OpenAI, en apelación).
- **Abogados:** Aconsejan documentar la implicación humana y actuar con cautela. No hay consenso sobre el umbral exacto de "implicación suficiente."
- **Comunidad de software libre:** Dividida. El kernel de Linux acepta código asistido por IA con atribución. QEMU/Gentoo lo prohíben. La FSF está preocupada pero no tiene una política definitiva.
- **Sentimiento general:** Pragmatismo cauteloso. La mayoría de los desarrolladores utilizan herramientas de IA siendo conscientes de las cuestiones legales no resueltas.

### P3: Habría algún problema para licenciar JNEXT bajo GPL-3.0?

**No hay problemas significativos.** De hecho, la GPLv3 es posiblemente la elección más natural:

- El núcleo FUSE Z80 (GPLv2-o-posterior) requiere que la obra combinada sea compatible con la GPL. La GPLv3 cumple este requisito.
- spdlog (MIT) y otras dependencias son permisivas y compatibles.
- Usted tiene una pretensión razonable sobre el copyright de las porciones generadas por IA dada su dirección creativa.
- **El único riesgo teórico:** Si su pretensión de copyright fuera impugnada posteriormente, algunas porciones podrían considerarse de dominio publico. Esto no haría ilegal su distribución, pero podría debilitar la aplicación del copyleft para esas porciones. Dado el carácter especializado del código de emulador, este riesgo es muy bajo en la practica.

### P4: Seria prudente distribuir solo en formato binario?

**No, esto seria contraproducente.** Véase la Sección 3.4 anterior. La licencia GPL del núcleo FUSE Z80 exige la disponibilidad del código fuente. Ocultar el código fuente violaría las propias licencias que ha incorporado y no proporcionaría ninguna protección legal significativa.

### P5: Estaría expuesto a demandas si publicara bajo GPLv3?

**El riesgo practico es muy bajo para JNEXT específicamente:**

- **Pretensión de contaminación por datos de entrenamiento:** Muy improbable. JNEXT implementa hardware del ZX Spectrum Next altamente especializado. El código está dirigido por una especificación VHDL, y el dominio es tan nicho que la reproducción literal de datos de entrenamiento es improbable. Ningún desarrollador ha sido demandado por usar código generado por IA en sus proyectos — las demandas se dirigen a los proveedores de IA, no a los usuarios finales.

- **Impugnación del copyright:** Alguien podría argumentar teóricamente que usted no es titular de un copyright valido. Pero esto no resultaría en indemnizaciones — en el peor de los casos, parte del código estaría en el dominio publico.

- **Impugnación del cumplimiento de la GPL:** Si cumple adecuadamente con los términos de la GPL (disponibilidad del código fuente, avisos de copyright, texto de la licencia), este riesgo es insignificante. Su configuración actual (archivo LICENSE, atribución en README, código de terceros incorporado con avisos originales) ya es solida.

- **Acusación de "blanqueo de licencias" al estilo Chardet:** No aplicable. JNEXT no es una reescritura de un emulador existente para cambiar su licencia. Es una aplicación nueva, y el núcleo FUSE Z80 conserva su licencia y atribución originales.

### P6: Existe consenso sobre la contaminación por datos de entrenamiento?

**No hay consenso, pero el perfil de riesgo de JNEXT es favorable:**

- La preocupación por la "contaminación" es más fuerte para patrones de código comunes y ampliamente replicados (frameworks web, funciones de utilidad, algoritmos estándar). JNEXT opera en un dominio altamente especializado — emulación de hardware del ZX Spectrum Next — donde el corpus de entrenamiento de código similar es reducido.
- La implementación está dirigida por una especificación autoritativa (fuente VHDL), no por la reproducción de patrones de bases de código existentes. Cuando el código debe seguir un patrón específico, es porque el hardware lo dicta (scenes a faire), no por contaminación.
- El núcleo FUSE Z80 — la coincidencia más obvia con código de emulador existente — está debidamente incorporado y licenciado, no generado por IA.

### P7: Estar ubicado en España afecta a la situación?

**Ventajas de España/UE para JNEXT:**

- Los requisitos de transparencia del EU AI Act (agosto de 2026) obligaran a los proveedores de IA a ser más claros sobre los datos de entrenamiento, reduciendo la incertidumbre sobre contaminación con el tiempo.
- La doctrina de derechos morales de la UE es fuerte en España — sus derechos como director creativo están bien protegidos.
- AESIA está publicando orientaciones de cumplimiento.

**Consideraciones de España/UE:**

- La sentencia GEMA v. OpenAI otorga mayor respaldo a las pretensiones de contaminación en Europa. Sin embargo, ese caso se refirió a la reproducción literal de letras de canciones, algo muy alejado del código especializado de emulador.
- Ningún tribunal español ha abordado específicamente el copyright del código generado por IA.

**Conclusión:** Estar en España no crea un riesgo adicional para su situación. La dirección de la UE favorece la transparencia de los proveedores de IA (lo que le beneficia) mientras que potencialmente crea reglas más estrictas que afectan principalmente a los proveedores, no a los usuarios finales como usted.

---

## 5. Conclusiones y recomendaciones

### Evaluación general

JNEXT se encuentra en una **posición solida** para su publicación bajo GPL-3.0. Las características del proyecto — dominio especializado, implementación dirigida por VHDL, código de terceros debidamente atribuido, dirección humana sustancial y documentada — lo sitúan en el extremo favorable del espectro para obras asistidas por IA. El panorama legal está evolucionando, pero nada en la legislación actual prohíbe ni pone en peligro materialmente lo que usted está haciendo.

### Recomendaciones

#### 1. Publique bajo GPL-3.0 — su configuración actual ya es solida

Su infraestructura de licenciamiento existente es buena:
- Archivo `LICENSE` con el texto completo de la GPLv3 ✓
- Aviso de copyright (Copyright (C) 2025-2026 Jorge Gonzalez Villalonga) ✓
- `README.md` con aviso de GPL ✓
- Núcleo FUSE Z80 debidamente incorporado con copyright original y `COPYING.GPL2` ✓
- spdlog como submódulo git con licencia MIT intacta ✓

El riesgo practico es bajo, y esperar a una claridad legal llevara anos sin garantía de un resultado más favorable.

#### 2. Documente su implicación creativa

Ya dispone de documentación solida:
- `EMULATOR-DESIGN-PLAN.md` — plan de diseño detallado de 9 fases con decisiones arquitectónicas
- `FPGA-REPO-ANALYSIS.md` — demuestra que la fuente VHDL es la especificación autoritativa
- `CLAUDE.md` — documenta la metodología de desarrollo
- Historial de git — muestra el desarrollo iterativo a lo largo de múltiples sesiones

**Adiciones recomendadas:**
- Añada una breve nota al README o a un archivo `CONTRIBUTORS.md` indicando que el código fue desarrollado con la asistencia de Claude Code (Anthropic), siendo usted el arquitecto principal y director creativo. La transparencia genera confianza y sigue el modelo del kernel de Linux.
- Mantenga intactos el `EMULATOR-DESIGN-PLAN.md`, el historial de git y los archivos de prompts diarios — son su evidencia más sólida de dirección creativa humana sostenida. El directorio `.prompts/` contiene archivos de prompts diarios con fecha (por ejemplo, `2026-03-18.md` a `2026-03-22.md`) que sirven como trazas exhaustivas del proceso creativo: las tareas específicas solicitadas, las decisiones de diseño tomadas, la retroalimentación de las iteraciones y el estado de completitud de tareas para cada sesión de desarrollo. Estos son particularmente valiosos como evidencia de autoría humana porque documentan la dirección creativa, no solo el resultado.

#### 3. Añada encabezados de copyright a los archivos fuente

Actualmente, los archivos fuente principales en `src/` no contienen encabezados de copyright por archivo. Aunque el archivo `LICENSE` a nivel de proyecto proporciona cobertura, añadir encabezados refuerza su posición:

```cpp
// Copyright (C) 2025-2026 Jorge Gonzalez Villalonga
// SPDX-License-Identifier: GPL-3.0-or-later
```

El identificador SPDX es un formato estándar legible por maquina que hace inequívoca la identificación de licencias. Esta es una mejora menor pero que vale la pena.

#### 4. Revise en busca de contaminación obvia — bajo esfuerzo, alto valor

Antes de publicar, realice una revisión ligera:
- Busque cualquier código sospechosamente específico que parezca provenir de un emulador conocido (ZEsarUX, CSpect, FUSE más allá del núcleo Z80 incorporado).
- Busque comentarios residuales, nombres de variables o patrones de código que parezcan copiados de proyectos identificables.
- Dado el dominio especializado y el desarrollo dirigido por VHDL, el riesgo es muy bajo, pero un esfuerzo de buena fe es prudente.

#### 5. NO distribuya solo en binario

No puede hacerlo legalmente con código de terceros bajo GPLv2/GPLv3. La GPL exige la disponibilidad del código fuente.

#### 6. Tenga en cuenta los requisitos de etiquetado del EU AI Act

A partir de agosto de 2026, el Artículo 50 podría exigir el etiquetado de contenido generado o asistido por IA. Su proyecto probablemente entraría en la categoría de "asistido por IA" (usted dirigió el desarrollo). Monitorice las directrices finales (previstas para junio de 2026) y añada el etiquetado apropiado si fuera necesario. Una nota en `CONTRIBUTORS.md` o en el README sobre la asistencia de Claude Code probablemente satisfaría este requisito.

#### 7. El asesoramiento legal es opcional pero está disponible

Para un proyecto personal/hobby, el riesgo es lo suficientemente bajo como para que el asesoramiento legal formal no sea estrictamente necesario. Sin embargo, si el proyecto adquiere importancia comercial o alta visibilidad, organizaciones como el Software Freedom Law Center o la FSFE pueden proporcionar orientación.

---

## 6. Preguntas adicionales de interés

### 6.1 Términos de servicio de Anthropic

Los términos de servicio de Anthropic para Claude deben revisarse para confirmar que usted conserva los derechos sobre la salida generada. La mayoría de los proveedores de IA (incluido Anthropic) otorgan explícitamente a los usuarios la titularidad sobre los resultados. Verifique esto para su plan/nivel de suscripción específico.

### 6.2 La no-cuestión de la "sala limpia" para JNEXT

La preocupación de "blanqueo de licencias" al estilo chardet no se aplica a JNEXT. Usted no está utilizando IA para reescribir un emulador existente con el fin de cambiar su licencia. Esta construyendo un nuevo emulador a partir de una especificación de hardware (fuente VHDL), y la única pieza de código de emulador existente que utiliza (el núcleo FUSE Z80) conserva su licencia original. Si acaso, su enfoque es lo opuesto al blanqueo de licencias — usted abiertamente atribuye y preserva las licencias originales.

### 6.3 Política de datos de entrenamiento de GitHub

GitHub anuncio (marzo de 2026) que Copilot utilizara los datos de interacción de usuarios Free, Pro y Pro+ para entrenar modelos de IA de forma predeterminada. Revise su configuración de GitHub si desea excluirse antes de publicar el repositorio.

### 6.4 La ventaja de scenes a faire

El desarrollo de emuladores tiene una ventaja legal natural: gran parte del código está dictado por el hardware que se emula. Un decodificador de instrucciones Z80, una implementación de temporizado de ULA o un manejador de registros NextREG deben funcionar de una manera especifica para ser correctos. Este requisito funcional limita el rango de expresión creativa y refuerza una defensa de scenes a faire contra pretensiones de contaminación. La fuente VHDL actúa como una especificación funcional que restringe el espacio de implementación.

### 6.5 Ruta de actualización de GPLv2-o-posterior a GPLv3

El núcleo FUSE Z80 está licenciado como "GPLv2-o-posterior." Al incluirlo en un proyecto GPLv3, usted está ejerciendo la opción "o posterior." Esto está explícitamente permitido por la GPL y es una practica bien establecida. No se necesita permiso adicional de los autores de FUSE.

### 6.6 Preparación para el futuro: La definición evolutiva de "autoría"

El umbral de "implicación humana suficiente" permanece indefinido. Su nivel de implicación — dirigiendo un proyecto de emulador complejo y multifase con arquitectura documentada, referencias cruzadas con VHDL y pruebas iterativas — es solido por cualquier estándar razonable. Futuros casos judiciales acabaran definiendo la linea con más precisión, pero usted se encuentra claramente en el lado favorable de dondequiera que esa linea se trace probablemente.

---

## 7. Fuentes

### Resoluciones judiciales y decisiones legales
- [Thaler v. Perlmutter — Opinión del Circuito de D.C. (2025)](https://media.cadc.uscourts.gov/opinions/docs/2025/03/23-5233.pdf)
- [El Tribunal Supremo deniega el certiorari en Thaler v. Perlmutter (2026)](https://www.bakerdonelson.com/supreme-court-denies-certiorari-in-thaler-v-perlmutter-ai-cannot-be-an-author-under-the-copyright-act)
- [GEMA v. OpenAI — Sentencia del Tribunal Regional de Munich (2025)](https://www.twobirds.com/en/insights/2025/landmark-ruling-of-the-munich-regional-court-(gema-v-openai)-on-copyright-and-ai-training)
- [Tribunal Regional Superior de Munich sobre memorización en el entrenamiento de IA](https://www.euipo.europa.eu/en/law/recent-case-law/the-higher-regional-court-of-munich-considered-memorization-and-temporary-copies-occurred-in-model-training-as-infringing-reproductions-of-works)
- [Litigio de propiedad intelectual de GitHub Copilot — Saveri Law Firm](https://www.saverilawfirm.com/our-cases/github-copilot-intellectual-property-litigation)
- [Actualización de la demanda de GitHub Copilot (feb. 2026)](https://patentailab.com/doe-v-github-lawsuit-explained-ai-copyright-rules/)

### Oficina de Copyright de EE.UU.
- [Copyright e Inteligencia Artificial — Oficina de Copyright de EE.UU.](https://www.copyright.gov/ai/)
- [Principales conclusiones sobre copyright e IA del informe de 2025](https://ipandmedialaw.fkks.com/post/102jyvb/key-insights-on-copyright-and-ai-from-the-u-s-copyright-offices-2025-report)
- [IA generativa y derecho de autor — Servicio de Investigación del Congreso](https://www.congress.gov/crs-product/LSB10922)

### UE y España
- [Copyright de obras generadas por IA: Enfoques en la UE y más allá — Parlamento Europeo](https://www.europarl.europa.eu/thinktank/en/document/EPRS_BRI(2025)782585)
- [Informe del Parlamento Europeo sobre copyright e IA generativa (2026)](https://www.europarl.europa.eu/doceo/document/A-10-2026-0019_EN.html)
- [Cumplimiento de copyright bajo el EU AI Act para proveedores de GPAI — Clifford Chance](https://www.cliffordchance.com/insights/resources/blogs/ip-insights/2025/10/copyright-compliance-under-the-eu-ai-act-for-gpai-model-providers.html)
- [España: orientación de AESIA sobre IA y código de la UE — Baker McKenzie](https://www.bakermckenzie.com/en/insight/publications/2026/02/spain-dpa-on-ai-images-and-new-eu-code)
- [La UE sigue atrapada en un dilema de copyright e IA — Bruegel](https://www.bruegel.org/analysis/european-union-still-caught-ai-copyright-bind)

### GPL, código abierto e IA
- [Teoría de la propagación de la GPL a modelos de IA (2025)](https://shujisado.org/2025/11/27/gpl-propagates-to-ai-models-trained-on-gpl-code/)
- [Entrenamiento con código abierto: que dicen las licencias sobre la IA — Terms.Law](https://www.terms.law/2025/12/05/training-on-open-source-code-what-gpl-mit-and-other-licenses-actually-say-about-ai/)
- [Viola el código generado por IA las licencias de código abierto? — TechTarget](https://www.techtarget.com/searchenterpriseai/tip/Examining-the-future-of-AI-and-open-source-software)
- [Puede la IA blanquear licencias de código abierto? — Mr. Latte](https://www.mrlatte.net/en/stories/2026/03/05/relicensing-with-ai-assisted-rewrite/)

### El incidente de Chardet
- [La disputa de Chardet muestra como la IA acabara con el licenciamiento de software — The Register](https://www.theregister.com/2026/03/06/ai_kills_software_licensing/)
- [Se puede relicenciar código abierto reescribiéndolo con IA? — Open Source Guy](https://shujisado.org/2026/03/10/can-you-relicense-open-source-by-rewriting-it-with-ai-the-chardet-7-0-dispute/)
- [Pueden los agentes de programación relicenciar el código abierto? — Simon Willison](https://simonwillison.net/2026/Mar/5/chardet/)
- [Blanqueo de licencias y la muerte de la sala limpia — ShiftMag](https://shiftmag.dev/license-laundering-and-the-death-of-clean-room-8528/)

### Kernel de Linux y políticas de la comunidad
- [Asistentes de programación con IA — Documentación del kernel de Linux](https://docs.kernel.org/process/coding-assistants.html)
- [Política de IA generativa de la Linux Foundation](https://www.linuxfoundation.org/legal/generative-ai)
- [La FSF sobre copilotos de código con IA](https://www.fsf.org/licensing/copilot/on-the-nature-of-ai-code-copilots)
- [La FSF en FOSDEM 2025](https://www.fsf.org/blogs/licensing/fsf-at-fosdem-2025)
- [FSFE: Inteligencia Artificial y Software Libre](https://fsfe.org/freesoftware/artificial-intelligence.en.html)

### Análisis general
- [Navegando el código generado por IA: titularidad y responsabilidad — MBHB](https://www.mbhb.com/intelligence/snippets/navigating-the-legal-landscape-of-ai-generated-code-ownership-and-liability-challenges/)
- [Contenido generado por IA y derecho de autor — Built In](https://builtin.com/artificial-intelligence/ai-copyright)
- [Inteligencia artificial y copyright — Wikipedia](https://en.wikipedia.org/wiki/Artificial_intelligence_and_copyright)
- [Preocupaciones y mejores practicas sobre copyright e IA generativa (2026)](https://research.aimultiple.com/generative-ai-copyright/)
- [Cambios en la política de datos de entrenamiento de IA de GitHub (marzo de 2026) — The Register](https://www.theregister.com/2026/03/26/github_ai_training_policy_changes/)

---

*Aviso legal: Este informe es un análisis de investigación y no constituye asesoramiento jurídico. Para decisiones con consecuencias legales o financieras significativas, consulte a un abogado cualificado en propiedad intelectual de su jurisdicción.*
