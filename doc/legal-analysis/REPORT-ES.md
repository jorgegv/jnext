# Código generado por IA y licencias: un análisis exhaustivo

**Fecha:** 27 de marzo de 2026

**Preparado para:** Jorge G.V.

**Asunto:** Estado legal actual del código generado por IA en relación con licencias, copyright y publicación bajo GPL-3.0

---

## Índice

1. [Comprensión de su situación y preocupaciones](#1-comprensión-de-su-situación-y-preocupaciones)
2. [Estado actual de la cuestión](#2-estado-actual-de-la-cuestión)
3. [Análisis detallado](#3-análisis-detallado)
4. [Respuestas a sus preguntas especificas](#4-respuestas-a-sus-preguntas-especificas)
5. [Conclusiones y recomendaciones](#5-conclusiones-y-recomendaciones)
6. [Preguntas adicionales de interés](#6-preguntas-adicionales-de-interés)
7. [Fuentes](#7-fuentes)

---

## 1. Comprensión de su situación y preocupaciones

Usted esta desarrollando una aplicación de gran envergadura casi en su totalidad a través de Claude Code, un asistente de programación basado en IA, con una autoría directa de código mínima por su parte. La aplicación se encuentra almacenada en un repositorio privado de GitHub e incorpora código existente con licencia GPL-3.0 procedente de otros repositorios. Le gustaría publicar el proyecto completo bajo la licencia GPL-3.0, pero duda debido a las incertidumbres legales.

Sus preocupaciones principales son:

- **Titularidad del copyright**: Si puede reclamar el copyright sobre código generado predominantemente por una herramienta de IA, y que posición legal tiene dicho código.
- **Validez de la licencia**: Si puede aplicar de forma efectiva la licencia GPL-3.0 a código generado por IA, dado que la concesión de licencias presupone la titularidad del copyright.
- **Exposición legal**: Si la publicación del código fuente podría exponerle a demandas, ya sea por parte de titulares de derechos de autor cuyo código pueda haber sido ingerido durante el entrenamiento de la IA, o de terceros que cuestionen su derecho a licenciar el código.
- **Contaminación por datos de entrenamiento**: Si el código producido por el LLM puede estar "contaminado" por código protegido por copyright con el que el modelo fue entrenado, y que implicaciones tiene esto para usted.
- **Cuestiones jurisdiccionales**: Como afecta a su situación el hecho de estar radicado en España, dentro de la UE, y en que se diferencia del panorama legal estadounidense.
- **Mitigación de riesgos**: Si publicar únicamente binarios (en lugar del código fuente) reduciría su exposición mientras el panorama legal madura.

Se trata de un conjunto prudente de preocupaciones. El panorama legal en torno al código generado por IA esta genuinamente sin resolver, y hace bien en detenerse y evaluar antes de publicar.

---

## 2. Estado actual de la cuestión

### 2.1 Posición de la Oficina de Copyright de EE.UU.

La Oficina de Copyright de EE.UU. ha establecido, a través de múltiples decisiones y un informe formal (Parte 2, publicado el 29 de enero de 2025), que:

- **Se requiere autoría humana** para la protección del copyright. Las obras generadas únicamente por IA, sin una aportación creativa humana significativa, no son elegibles para la protección del copyright.
- **Las obras asistidas por IA pueden estar protegidas por copyright**, pero solo en la medida en que un autor humano haya contribuido con expresión creativa suficiente. La mera provisión de prompts a una IA generalmente no se considera una aportación creativa suficiente.
- El uso de IA como asistente en la creación, o la inclusión de material generado por IA en una obra mayor generada por humanos, **no impide automáticamente** la protección por copyright.
- La Oficina recomienda documentar la participación humana en el desarrollo asistido por IA.

### 2.2 Resoluciones judiciales clave

#### Thaler v. Perlmutter (jurisprudencia consolidada a marzo de 2026)

Este es el caso estadounidense de referencia sobre autoría por IA:

- **Tribunal de Distrito (agosto de 2023):** Resolvió que la IA no puede ser autor según la Ley de Copyright.
- **Tribunal de Apelaciones del Circuito de D.C. (marzo de 2025):** Confirmo la sentencia, declarando que la autoría humana es un "requisito fundamental." De manera importante, el tribunal aclaro: *"El requisito de autoría humana no prohibe registrar obras que fueron hechas por o con la asistencia de inteligencia artificial."* El autor debe ser un humano --la persona que creo, opero o utilizo la IA-- no la maquina en si.
- **Tribunal Supremo de EE.UU. (2 de marzo de 2026):** Denegó el recurso de certiorari, convirtiendo esto en jurisprudencia efectivamente consolidada en Estados Unidos. La IA no puede ser autor, pero los humanos que usan IA pueden ser autores de las obras resultantes, dependiendo de su nivel de implicación creativa.

#### GEMA v. OpenAI (Munich, Alemania -- noviembre de 2025)

Un caso europeo de referencia:

- El Tribunal Regional de Munich resolvió que los modelos GPT de OpenAI **infringieron el copyright** al memorizar y reproducir letras de canciones de forma casi literal.
- El tribunal sostuvo que **la memorización de datos de entrenamiento constituye reproducción** según el derecho de copyright de la UE/Alemania.
- El tribunal rechazo la defensa de minería de textos y datos (TDM) de OpenAI, sosteniendo que la excepción de TDM se aplica únicamente a la fase analítica inicial, no a la memorización y reproducción de obras.
- Se ordeno a OpenAI cesar, pagar danos y perjuicios y divulgar información sobre ingresos.
- **Este caso esta en apelación** y eventualmente podría llegar al Tribunal de Justicia de la UE (TJUE).
- **Relevancia para su caso:** Esta sentencia establece (al menos en una jurisdicción de la UE) que si un modelo de IA puede reproducir contenido protegido por copyright de sus datos de entrenamiento, ello constituye infracción. Esto es directamente relevante para la preocupación sobre "contaminación."

#### Doe v. GitHub / Demanda contra GitHub Copilot (en curso)

- Presentada en noviembre de 2022 contra GitHub, Microsoft y OpenAI, alegando que Copilot fue entrenado con código de fuente abierta y lo reproduce sin la atribución ni el cumplimiento de licencia adecuados.
- La mayoría de las reclamaciones (incluidas las relativas a la DMCA) fueron desestimadas entre 2023 y 2024. Persiste un conjunto reducido de reclamaciones (teorías de incumplimiento contractual).
- El caso no ha producido una resolución definitiva sobre si el código generado por IA constituye una obra derivada de los datos de entrenamiento.
- **A principios de 2026, la demanda sigue sin resolverse.**

### 2.3 Marco legal de la UE

#### EU AI Act

- La EU AI Act entro en vigor y se esta implementando progresivamente. Las obligaciones de transparencia del Articulo 50 se aplicaran a partir de **agosto de 2026**.
- La normativa exige el etiquetado del contenido generado por IA con un sistema de dos niveles: contenido totalmente generado por IA frente a contenido asistido por IA.
- La Agencia Española de Supervisión de la IA (AESIA) ha publicado 16 documentos de orientación para el cumplimiento.

#### Informe del Parlamento Europeo sobre Copyright e IA (2026)

- El Parlamento Europeo voto un informe sobre "Copyright e IA Generativa" en enero de 2026, proponiendo:
  - Mejora de las licencias y la remuneración por el uso de datos de entrenamiento.
  - Amplia aplicación territorial: el derecho de copyright de la UE debería aplicarse a los modelos de IA comercializados en el mercado de la UE, **independientemente de donde se haya realizado el entrenamiento**.
  - Mayor transparencia por parte de los proveedores de IA sobre los datos de entrenamiento.
- Esto aun no es ley, pero señala la dirección de la política de la UE.

#### Consideraciones especificas para España

- España sigue el derecho de copyright de la UE (directivas europeas transpuestas). La ley española de propiedad intelectual (Ley de Propiedad Intelectual, Real Decreto Legislativo 1/1996) exige autoría humana para la protección por copyright, en coherencia con la posición general de la UE.
- AESIA esta publicando activamente orientaciones sobre cumplimiento en materia de IA.
- No existe aun ninguna sentencia judicial española especifica sobre copyright de código generado por IA, pero los desarrollos a nivel de la UE (GEMA v. OpenAI, informes del PE) configuraran el panorama legal.

### 2.4 Perspectivas internacionales

- **Japón:** Históricamente ha sido mas permisivo en relación con la IA y el copyright. La ley de copyright japonesa permite ciertos usos de entrenamiento de IA sin autorización, aunque esto esta en debate continuo.
- **Reino Unido:** El Reino Unido tenia previamente una disposición (CDPA 1988, Sección 9(3)) que otorgaba copyright para obras generadas por ordenador a "la persona que realizo los arreglos necesarios para la creación de la obra." Esta disposición es única a nivel mundial, pero no ha sido sometida a prueba con la IA generativa moderna.
- **Tendencia general:** La mayoría de las jurisdicciones convergen en exigir autoría humana para el copyright, mientras que los umbrales específicos de "cuanta participación humana" permanecen sin definir.

### 2.5 Posiciones de la comunidad de código abierto

#### Free Software Foundation (FSF)

- La FSF ha expresado su preocupación de que los LLM entrenados con código bajo licencia GPL podrían **infringir inadvertidamente las disposiciones de copyleft**.
- Abogan por la transparencia en la IA y argumentan que los principios de libertad del software deberían extenderse a los sistemas de IA.
- La FSF ha financiado artículos de investigación sobre copilotos de código IA y continua desarrollando su posición.
- **No se ha emitido ninguna declaración de política definitiva** específicamente sobre si los desarrolladores individuales pueden licenciar código generado por IA bajo GPL.

#### Política del kernel de Linux

El kernel de Linux ha adoptado la política mas detallada de cualquier proyecto importante:

- Las herramientas de IA deben cumplir todos los procesos de desarrollo y estándares de codificación existentes del kernel.
- **Los agentes de IA NO DEBEN añadir etiquetas Signed-off-by.** Solo los humanos pueden certificar el Developer Certificate of Origin (DCO).
- Las contribuciones asistidas por IA deben incluir una etiqueta `Assisted-by:` (por ejemplo, `Assisted-by: Claude:claude-3-opus`).
- El desarrollador humano asume **toda la responsabilidad** de revisar el código generado por IA, verificar el cumplimiento de licencias y asumir la responsabilidad de la contribución.
- Linus Torvalds ha declarado que las herramientas de IA deben tratarse de la misma forma que las ayudas de programación tradicionales.

#### Otros proyectos

- **QEMU y Gentoo** han prohibido las contribuciones generadas por IA, citando incertidumbre sobre copyright, preocupaciones de calidad e incompatibilidad con el DCO.
- **Debian** debatió las contribuciones de IA pero no llego a una decisión formal.
- No existe un consenso universal en la comunidad.

### 2.6 El incidente de "Chardet" y el blanqueo de licencias (marzo de 2026)

Un caso muy relevante y muy reciente:

- El mantenedor de `chardet` (una biblioteca Python de detección de codificación de caracteres) utilizo Claude para reescribir todo el código fuente y cambio la licencia de LGPL a MIT, alegando que se trataba de una "implementación en sala limpia."
- El autor original, Mark Pilgrim, objeto, calificándolo de violación explicita de la LGPL.
- La comunidad de código abierto critico ampliamente esto como **"blanqueo de licencias"** -- el uso de IA para eludir obligaciones de copyleft.
- La cuestión central: si una IA reescribe código con la misma funcionalidad pero diferente expresión, es una obra derivada?
- **Ningún tribunal ha resuelto sobre esto**, pero el incidente demuestra los riesgos reales y la sensibilidad de la comunidad en torno al código generado por IA y las licencias.

---

## 3. Análisis detallado

### 3.1 La paradoja del copyright para el código generado por IA

Existe una tensión fundamental en el centro de su situación:

1. **Para licenciar código (bajo GPL o cualquier licencia), necesita ser titular del copyright sobre el.** La GPL-3.0 es una licencia de copyright -- otorga permisos que solo el titular del copyright puede conceder.

2. **El código generado por IA puede no ser susceptible de protección por copyright** si la contribución humana fue insuficiente. Según el derecho tanto de EE.UU. como de la UE, el contenido puramente generado por IA probablemente pertenece al dominio publico.

3. **Si el código esta en el dominio publico, no puede licenciarlo efectivamente bajo GPL-3.0**, porque no hay nada que licenciar. Cualquiera podría tomar el código y utilizarlo sin restricción, anulando el propósito del copyleft.

Sin embargo, la realidad tiene matices:

- El "espectro de participación humana" importa. En un extremo: escribir un único prompt y aceptar la salida literalmente (probablemente no protegible por copyright a su nombre). En el otro: dirigir iterativamente la IA, revisar la salida, tomar decisiones creativas sobre arquitectura y diseño, editar y refinar código, integrar componentes (probablemente protegible por copyright, al menos en parte).

- **Su situación** -- desarrollar una "gran aplicación" a través de Claude Code -- probablemente implica una dirección creativa sustancial: decidir que funcionalidades construir, como diseñar la arquitectura del sistema, revisar y aceptar o rechazar sugerencias, integrar con código GPL existente, probar e iterar. Esto le coloca en una posición mas solida que alguien que acepta ciegamente la salida de la IA.

### 3.2 El riesgo de contaminación

El argumento de "contaminación" tiene dos formas distintas:

**a) Reproducción literal:** La IA reproduce fragmentos reconocibles de código protegido por copyright de sus datos de entrenamiento. Este es el riesgo jurídicamente mas claro. La sentencia GEMA v. OpenAI confirma (en Alemania) que esto puede constituir infracción de copyright. La investigación ha demostrado que las IAs generadoras de código pueden en ocasiones reproducir fragmentos literales, incluidas cabeceras de licencia.

**b) Similitud estructural:** La IA produce código que, sin ser literal, es sustancialmente similar a código protegido especifico con el que fue entrenada. Esto es mas difícil de detectar y jurídicamente mas difuso.

**Evaluación practica del riesgo:** Los LLM modernos (incluido Claude) se entrenan con técnicas para reducir la memorización y la reproducción literal. El riesgo de reproducción literal de grandes bloques de código es bajo pero no nulo, especialmente para patrones de código muy populares o distintivos. El riesgo de similitud estructural inadvertida es mayor pero mas difícil de perseguir judicialmente.

### 3.3 La complicación especifica de la GPL

Su aplicación ya contiene código GPL-3.0 de otros repositorios. Esto crea una capa adicional:

- El código GPL-3.0 existente **esta** protegido por copyright de sus autores originales y **tiene** una licencia valida.
- La GPL-3.0 exige que las obras derivadas también se distribuyan bajo GPL-3.0.
- Si usted esta combinando este código GPL con código generado por IA en un único programa, la disposición de copyleft de la GPL exigiría que la obra combinada completa sea GPL-3.0.
- Para las porciones generadas por IA: si son protegibles por copyright (debido a su participación creativa), puede licenciarlas bajo GPL-3.0. Si no son protegibles (dominio publico), aun pueden *incluirse* en un proyecto GPL-3.0 -- el código de dominio publico puede incorporarse a cualquier proyecto -- pero otros podrían teóricamente extraer esas porciones y utilizarlas sin las restricciones de la GPL.

### 3.4 La cuestión del "solo binarios"

Publicar únicamente binarios no elimina el riesgo legal; solo cambia su naturaleza:

- Si el código generado por IA contiene fragmentos memorizados protegidos por copyright, la infracción existe en el momento de la reproducción (cuando la IA lo genero y cuando usted lo compilo), no solo cuando se publica el código fuente.
- La distribución de binarios sigue requiriendo el cumplimiento de licencia para los componentes GPL que incorporo.
- La propia GPL-3.0 exige que ponga el código fuente a disposición de los destinatarios del binario. Distribuir binarios bajo GPL-3.0 sin proporcionar el código fuente violaría los términos de la GPL del código de terceros que incorporo.
- Ocultar el código fuente simplemente retrasa el descubrimiento; no elimina el problema legal subyacente.

---

## 4. Respuestas a sus preguntas especificas

### P1: Quien es titular del copyright del código generado por IA?

**Respuesta breve:** Depende del nivel de participación creativa humana.

- **En EE.UU. (jurisprudencia consolidada tras Thaler v. Perlmutter, confirmada por la denegación de certiorari del Tribunal Supremo, marzo de 2026):** La IA en si no puede ser autor ni titular de copyright. Si un humano aporta expresión creativa suficiente -- mediante decisiones de diseño, refinamiento iterativo, edición e integración -- el humano es el autor. Si la salida de la IA se utiliza con una aportación creativa humana mínima, puede no ser protegible por copyright en absoluto y estaría efectivamente en el dominio publico.

- **En la UE/España:** La situación es similar. El derecho de copyright de la UE requiere creación intelectual humana. La UE no tiene un equivalente a la disposición del Reino Unido sobre "obras generadas por ordenador." Las obras generadas puramente por IA sin una contribución creativa humana significativa probablemente no son protegibles por copyright.

- **Para su situación especifica:** Dado que usted esta dirigiendo el desarrollo de una aplicación completa (tomando decisiones arquitectónicas, eligiendo funcionalidades, revisando e iterando sobre la salida, integrando con código existente), tiene un argumento razonable de que usted es el autor. Sin embargo, el umbral exacto de "participación creativa suficiente" al utilizar herramientas de IA como Claude Code no ha sido sometido a prueba ante los tribunales.

### P2: Cual es el estado actual de opinión entre desarrolladores de software libre, abogados y tribunales?

El panorama esta fragmentado:

- **Tribunales:** Coinciden en que se requiere autoría humana (Thaler v. Perlmutter). Coinciden en que las obras asistidas por IA *pueden* estar protegidas por copyright (informe Parte 2 de la Oficina de Copyright de EE.UU.). La sentencia GEMA v. OpenAI en Alemania determino que la memorización por IA de datos de entrenamiento constituye infracción de copyright.

- **Abogados:** Generalmente aconsejan documentar la participación humana, ser prudente al reclamar copyright sobre salidas de IA mínimamente modificadas y estar atentos a la evolución de la jurisprudencia. No hay consenso sobre donde se sitúa exactamente la linea de "participación humana suficiente."

- **Comunidad de software libre:** Dividida. El kernel de Linux acepta contribuciones asistidas por IA con atribución, atribuyendo toda la responsabilidad al desarrollador humano. QEMU y Gentoo las prohiben. La FSF esta preocupada por la erosión del copyleft pero no ha emitido una política definitiva. El incidente de chardet (marzo de 2026) ha avivado las preocupaciones de la comunidad sobre el "blanqueo de licencias."

- **Sentimiento general:** Pragmatismo cauteloso. La mayoría de los desarrolladores y organizaciones están utilizando herramientas de IA pero son conscientes de las cuestiones legales sin resolver.

### P3: Habría algún problema en licenciar mi código bajo GPL-3.0?

**Respuesta practica:** Puede hacerlo, pero con salvedades.

- El código GPL-3.0 existente de otros repositorios conserva su licencia. No hay problema en ese aspecto.
- Para las porciones generadas por IA: si su participación creativa es suficiente (lo cual, como director y revisor de toda la aplicación, es plausible), puede reclamar el copyright y licenciar bajo GPL-3.0.
- **El riesgo:** Si su reclamación de copyright sobre las porciones generadas por IA es cuestionada posteriormente y considerada insuficiente, dichas porciones podrían ser declaradas dominio publico. Esto no haría que su publicación fuera *ilegal*, pero podría significar que las disposiciones de copyleft de la GPL-3.0 son inaplicables para esas porciones. Alguien podría extraer las partes generadas por IA y utilizarlas sin las restricciones de la GPL.
- **El riesgo de contaminación:** Si Claude reprodujo código protegido por copyright de sus datos de entrenamiento en su proyecto, el titular original del copyright podría (en teoría) objetar. Este riesgo existe independientemente de la licencia que elija.

### P4: Seria prudente publicar solo en formato binario?

**No, esto seria en realidad contraproducente por varias razones:**

1. **La GPL-3.0 exige la disponibilidad del código fuente.** Dado que su aplicación contiene código GPL-3.0 de otros repositorios, esta legalmente obligado por dichas licencias a proporcionar el código fuente a quien reciba el binario. Publicar solo binarios violaría los términos de la GPL del código que incorporo.

2. **Ocultar el código fuente no elimina el riesgo de contaminación.** Si la IA produjo código infractor, la infracción existe independientemente de que publique el código fuente o no.

3. **La transparencia es su aliada.** Publicar el código fuente permite a la comunidad revisarlo y señalar posibles problemas. Esto es mejor que tener una infracción silenciosamente incrustada en su binario.

4. **Socavaría su credibilidad.** Publicar un proyecto supuestamente GPL-3.0 sin código fuente constituye una violación de la GPL y dañaría la confianza.

### P5: Estaría expuesto a demandas si publica bajo GPLv3?

**La respuesta honesta es: si, existe cierta exposición teórica, pero el riesgo practico es bajo.**

Escenarios de demanda posibles (pero improbables):

- **Titular de copyright cuyo código fue memorizado por la IA:** Si Claude reprodujo fragmentos reconocibles del código protegido de alguien en su proyecto, esa persona podría teóricamente demandar por infracción. La sentencia GEMA v. OpenAI respalda esta teoría en Europa. Sin embargo: (a) los LLM modernos están diseñados para minimizar esto, (b) el demandante necesitaría descubrir la similitud y probarla, (c) el objetivo mas probable seria Anthropic (el proveedor de la IA) y no usted como usuario final.

- **Impugnación de su reclamación de copyright:** Alguien podría argumentar que usted no posee un copyright valido sobre las porciones generadas por IA, socavando su licencia GPL. Pero esto no resultaría en *danos y perjuicios* contra usted -- en el peor de los casos, el código seria tratado como dominio publico.

- **Impugnación de cumplimiento de la GPL:** Si no cumple adecuadamente con los términos de la GPL-3.0 para el código de terceros que incorporo (por ejemplo, no proporcionar el código fuente, no incluir los avisos adecuados), los autores originales podrían hacer valer sus derechos de GPL.

**Lo que NO es un riesgo realista:**

- Ser demandado simplemente por publicar código generado por IA bajo GPL-3.0 no es algo que ninguna teoría legal actual respalde como causa de acción.
- La demanda contra GitHub Copilot se dirige contra los proveedores de IA, no contra los desarrolladores individuales que utilizan las herramientas.

### P6: Existe consenso sobre la contaminación por datos de entrenamiento?

**No hay consenso, pero están surgiendo posiciones:**

- **El campo de la "contaminación":** Argumenta que, dado que los LLM se entrenan con grandes cantidades de código protegido por copyright (bajo diversas licencias), sus salidas son inherentemente sospechosas y pueden constituir obras derivadas sin licencia. La FSF se inclina hacia esta posición. La sentencia GEMA v. OpenAI valida parcialmente esta postura (al menos para la reproducción literal o casi literal).

- **El campo de la "herramienta":** Argumenta que los LLM son herramientas, como los compiladores o los IDE, y su salida debería tratarse como cualquier otro código escrito por el usuario humano. Linus Torvalds y muchos desarrolladores pragmáticos sostienen esta posición. La postura de la Oficina de Copyright de EE.UU. (de que las obras asistidas por IA pueden estar protegidas por copyright) respalda implícitamente esta visión.

- **El punto medio (posición mayoritaria):** La mayoría de los expertos legales y organizaciones reconocen que el riesgo es real pero dependiente del contexto. El riesgo es mayor cuando: (a) la IA reproduce código literal o casi literalmente, (b) la fuente de los datos de entrenamiento es identificable, (c) el código original tiene una expresión creativa distintiva. El riesgo es menor cuando: la salida es código genérico y funcional que podría escribirse de muchas formas.

- **Realidad practica:** Ningún desarrollador ha sido demandado por utilizar código generado por IA en sus proyectos. Las demandas se dirigen contra los proveedores de IA (Microsoft, GitHub, OpenAI), no contra los usuarios finales. Esto podría cambiar, pero es el estado actual.

### P7: El hecho de estar radicado en España afecta a la situación?

**Si, su jurisdicción importa:**

**Ventajas de España/UE:**

- La EU AI Act exigirá transparencia a los proveedores de IA sobre los datos de entrenamiento (vigente desde agosto de 2026), lo que eventualmente podría proporcionarle mejores herramientas para evaluar el riesgo de contaminación.
- La sentencia GEMA v. OpenAI, si se confirma y se adopta de forma mas amplia, podría obligar a los proveedores de IA a depurar las practicas relativas a datos de entrenamiento, reduciendo el riesgo de contaminación con el tiempo.
- La doctrina de "derechos morales" del copyright de la UE (fuerte en España) significa que usted conserva ciertos derechos como director creativo de la obra, mas allá del copyright económico.
- AESIA esta publicando activamente orientaciones sobre cumplimiento en materia de IA.

**Complicaciones de España/UE:**

- La sentencia GEMA v. OpenAI establece (en Alemania, con valor persuasivo en la UE) que datos de entrenamiento memorizados = reproducción. Esto significa que la teoría de la contaminación tiene *mas* respaldo legal en Europa que en EE.UU.
- La propuesta de expansión territorial del Parlamento Europeo del derecho de copyright a los modelos de IA "comercializados en el mercado de la Unión" podría crear obligaciones adicionales.
- Ningún tribunal español se ha pronunciado específicamente sobre el copyright de código generado por IA.

**Comparación con EE.UU.:**

- En EE.UU., la escasa supervivencia de reclamaciones en la demanda contra Copilot sugiere que es difícil (aunque no imposible) ganar demandas de copyright contra generadores de código IA.
- EE.UU. tiene una doctrina de "fair use" (uso justo) mas solida, que puede proteger el entrenamiento de IA mas que las excepciones de TDM mas limitadas de la UE.
- La Oficina de Copyright de EE.UU. ha sido mas explicita sobre cuando las obras asistidas por IA *pueden* estar protegidas por copyright.

**Conclusión para su jurisdicción:** Estar en España significa que opera bajo el derecho de copyright de la UE, que tiende hacia una regulación mas estricta de la IA y una protección mas fuerte para los titulares de copyright sobre datos de entrenamiento. Esto tiene un doble filo: puede dificultar que otros cuestionen su uso de herramientas de IA (porque la carga recae sobre los proveedores de IA), pero también significa que las reclamaciones por contaminación tienen un respaldo legal mas fuerte si alguien descubre su código en su salida.

---

## 5. Conclusiones y recomendaciones

### Valoración general

Su situación es jurídicamente navegable pero requiere una actuación reflexiva. El derecho esta evolucionando rápidamente, y ninguna sentencia única resuelve definitivamente todas sus preguntas. Sin embargo, el equilibrio entre el derecho actual, las políticas y el riesgo practico respalda el siguiente enfoque:

### Recomendaciones

#### 1. Proceda a publicar bajo GPL-3.0, pero con la debida preparación

El riesgo practico de demandas es bajo, y el panorama legal -- aunque incierto -- no prohibe lo que usted esta haciendo. El código GPL-3.0 de terceros que ha incorporado le *obliga* a distribuir bajo GPL-3.0 en cualquier caso. No publicar por incertidumbre legal es una opción valida, pero es improbable que la incertidumbre se resuelva definitivamente en anos. Esperar aporta poco.

#### 2. Documente exhaustivamente su participación creativa

Este es el paso mas importante que puede dar para protegerse:

- Conserve registros de sus decisiones de diseño, elecciones arquitectónicas y dirección creativa.
- Documente su proceso de revisión e iteración -- los prompts que dio, las salidas que rechazo, los cambios que realizo.
- Este en condiciones de demostrar que ejerció juicio creativo, no que simplemente pulso un botón.
- Esta documentación es su evidencia de autoría humana en caso de impugnación.

#### 3. Añada atribución y transparencia claras

- En su repositorio, incluya una nota (por ejemplo, en el README o un archivo CONTRIBUTORS) indicando que el código fue desarrollado con la asistencia de Claude Code (Anthropic).
- Siga el modelo del kernel de Linux: atribuya la asistencia de IA de forma transparente. Esto genera confianza y demuestra buena fe.
- Atribuya adecuadamente todo el código GPL-3.0 de terceros que incorporo, con los avisos de copyright intactos.

#### 4. Revise el código en busca de contaminación evidente

- Antes de publicar, realice una revisión (manual o automatizada) en busca de código sospechosamente especifico que parezca haber sido reproducido de una biblioteca conocida.
- Busque comentarios residuales, nombres de variables o patrones de código que parezcan copiados de proyectos identificables.
- Las herramientas de detección de similitud de código (por ejemplo, MOSS, JPlag, o alternativas comerciales) pueden ayudar.
- Esto no necesita ser exhaustivo, pero un esfuerzo de buena fe reduce significativamente el riesgo.

#### 5. NO publique solo binarios

- No puede hacerlo legalmente con código GPL-3.0 de terceros en cualquier caso -- la GPL exige la disponibilidad del código fuente.
- La transparencia (publicar el código fuente) es en realidad una medida protectora, no una vulnerabilidad.

#### 6. Incluya un cumplimiento adecuado de la GPL-3.0

- Incluya el texto completo de la licencia GPL-3.0.
- Incluya avisos de copyright para todo el código GPL de terceros.
- Para su propio código, puede usar un aviso de copyright como: `Copyright (C) 2026 Jorge G.V.` -- esto afirma su reclamación como autor/director creativo de la obra.
- Incluya un archivo NOTICE o similar que explique la procedencia del proyecto.

#### 7. Considere obtener asesoramiento legal

- Si la aplicación tiene relevancia comercial o anticipa una visibilidad publica significativa, considere consultar a un abogado especializado en propiedad intelectual y licencias de software en España/UE.
- Organizaciones como el Software Freedom Law Center o la FSFE pueden proporcionar orientación.

---

## 6. Preguntas adicionales de interés

Durante esta investigación, surgieron varias preguntas adicionales que pueden ser relevantes para usted:

### 6.1 Condiciones de servicio del proveedor de IA

Deberían revisarse las condiciones de servicio de Anthropic para Claude para confirmar que usted conserva los derechos sobre el código generado a través de Claude Code. La mayoría de los proveedores de IA (incluido Anthropic) declaran explícitamente que los usuarios conservan la titularidad de las salidas. Verifique esto para su plan/nivel especifico.

### 6.2 El problema de la "sala limpia"

El incidente de chardet (marzo de 2026) plantea una cuestión critica: puede utilizarse la IA para implementaciones en sala limpia? El proceso tradicional de sala limpia exige que el implementador **no tenga acceso** al código original. Dado que los LLM fueron entrenados con grandes cantidades de código, inherentemente "tienen acceso" al original. Esto puede invalidar las reclamaciones de sala limpia para código generado por IA. Esto es relevante si alguna parte de su proyecto fue motivada con instrucciones del tipo "implementa algo como X" donde X es una biblioteca existente.

### 6.3 Cambio de política de datos de entrenamiento de GitHub

GitHub anuncio (marzo de 2026) que Copilot utilizara datos de interacción de usuarios Free, Pro y Pro+ para entrenar modelos de IA **por defecto**. Esto significa que sus interacciones con GitHub (aunque no necesariamente con Claude Code) podrían retroalimentar el entrenamiento de IA. Las cuentas Enterprise están exentas. Revise su nivel y configuración de GitHub.

### 6.4 Futuros requisitos de etiquetado de la UE

A partir de agosto de 2026, el Articulo 50 de la EU AI Act exigirá el etiquetado del contenido generado por IA. El Código de Practicas distingue entre contenido "totalmente generado por IA" y contenido "asistido por IA." Su aplicación probablemente entraría en la categoría de "asistida por IA" (dado que usted dirigió el desarrollo), pero debería estar atento a este requisito ya que las directrices finales se esperan para junio de 2026.

### 6.5 Seguros e indemnización

Algunos proveedores de IA ofrecen indemnización por reclamaciones de propiedad intelectual relacionadas con la salida de su IA (por ejemplo, Microsoft ofrece esto a usuarios de Copilot Enterprise). Verifique si Anthropic ofrece alguna protección similar para los usuarios de Claude Code. Esto podría ser una medida adicional de mitigación de riesgos.

### 6.6 La definición evolutiva de "autoría"

El umbral de "participación humana suficiente" para reclamar la autoría de obras asistidas por IA permanece sin definir. La Oficina de Copyright de EE.UU. dice que los prompts por si solos no son suficientes, pero el diseño iterativo, la revisión y la integración probablemente si lo son. La linea exacta sera trazada por futuros casos judiciales. Su nivel de participación (dirigir una aplicación completa) se sitúa en el lado mas fuerte de este espectro, pero no hay garantías.

---

## 7. Fuentes

### Sentencias judiciales y decisiones legales
- [Thaler v. Perlmutter -- Sentencia del Circuito de D.C. (2025)](https://media.cadc.uscourts.gov/opinions/docs/2025/03/23-5233.pdf)
- [El Tribunal Supremo deniega certiorari en Thaler v. Perlmutter (2026)](https://www.bakerdonelson.com/supreme-court-denies-certiorari-in-thaler-v-perlmutter-ai-cannot-be-an-author-under-the-copyright-act)
- [GEMA v. OpenAI -- Sentencia del Tribunal Regional de Munich (2025)](https://www.twobirds.com/en/insights/2025/landmark-ruling-of-the-munich-regional-court-(gema-v-openai)-on-copyright-and-ai-training)
- [Tribunal Superior Regional de Munich sobre memorización en el entrenamiento de IA](https://www.euipo.europa.eu/en/law/recent-case-law/the-higher-regional-court-of-munich-considered-memorization-and-temporary-copies-occurred-in-model-training-as-infringing-reproductions-of-works)
- [Litigio de propiedad intelectual de GitHub Copilot -- Saveri Law Firm](https://www.saverilawfirm.com/our-cases/github-copilot-intellectual-property-litigation)
- [Actualización de la demanda contra GitHub Copilot (feb. 2026)](https://patentailab.com/doe-v-github-lawsuit-explained-ai-copyright-rules/)

### Oficina de Copyright de EE.UU.
- [Copyright e Inteligencia Artificial -- Oficina de Copyright de EE.UU.](https://www.copyright.gov/ai/)
- [Conclusiones clave sobre copyright e IA del informe de 2025](https://ipandmedialaw.fkks.com/post/102jyvb/key-insights-on-copyright-and-ai-from-the-u-s-copyright-offices-2025-report)
- [IA generativa y derecho de copyright -- Servicio de Investigación del Congreso](https://www.congress.gov/crs-product/LSB10922)

### UE y España
- [Copyright de obras generadas por IA: enfoques en la UE y mas allá -- Parlamento Europeo](https://www.europarl.europa.eu/thinktank/en/document/EPRS_BRI(2025)782585)
- [Informe del Parlamento Europeo sobre Copyright e IA Generativa (2026)](https://www.europarl.europa.eu/doceo/document/A-10-2026-0019_EN.html)
- [Cumplimiento de copyright bajo la EU AI Act para proveedores de GPAI -- Clifford Chance](https://www.cliffordchance.com/insights/resources/blogs/ip-insights/2025/10/copyright-compliance-under-the-eu-ai-act-for-gpai-model-providers.html)
- [España: orientación de AESIA sobre IA y código de la UE -- Baker McKenzie](https://www.bakermckenzie.com/en/insight/publications/2026/02/spain-dpa-on-ai-images-and-new-eu-code)
- [La UE sigue atrapada en un dilema de copyright e IA -- Bruegel](https://www.bruegel.org/analysis/european-union-still-caught-ai-copyright-bind)

### GPL, código abierto e IA
- [Teoría de propagación de la GPL a modelos de IA (2025)](https://shujisado.org/2025/11/27/gpl-propagates-to-ai-models-trained-on-gpl-code/)
- [Entrenamiento con código abierto: lo que dicen las licencias sobre la IA -- Terms.Law](https://www.terms.law/2025/12/05/training-on-open-source-code-what-gpl-mit-and-other-licenses-actually-say-about-ai/)
- [Viola el código generado por IA las licencias de código abierto? -- TechTarget](https://www.techtarget.com/searchenterpriseai/tip/Examining-the-future-of-AI-and-open-source-software)
- [Puede la IA blanquear licencias de código abierto? -- Mr. Latte](https://www.mrlatte.net/en/stories/2026/03/05/relicensing-with-ai-assisted-rewrite/)

### El incidente de Chardet
- [La disputa de Chardet muestra como la IA acabara con las licencias de software -- The Register](https://www.theregister.com/2026/03/06/ai_kills_software_licensing/)
- [Se puede relicenciar código abierto reescribiéndolo con IA? -- Open Source Guy](https://shujisado.org/2026/03/10/can-you-relicense-open-source-by-rewriting-it-with-ai-the-chardet-7-0-dispute/)
- [Pueden los agentes de programación relicenciar código abierto? -- Simon Willison](https://simonwillison.net/2026/Mar/5/chardet/)
- [Blanqueo de licencias y la muerte de la sala limpia -- ShiftMag](https://shiftmag.dev/license-laundering-and-the-death-of-clean-room-8528/)

### Kernel de Linux y políticas de la comunidad
- [Asistentes de programación con IA -- Documentación del kernel de Linux](https://docs.kernel.org/process/coding-assistants.html)
- [Política de IA generativa de la Linux Foundation](https://www.linuxfoundation.org/legal/generative-ai)
- [FSF sobre copilotos de código IA](https://www.fsf.org/licensing/copilot/on-the-nature-of-ai-code-copilots)
- [FSF en FOSDEM 2025](https://www.fsf.org/blogs/licensing/fsf-at-fosdem-2025)
- [FSFE: Inteligencia Artificial y Software Libre](https://fsfe.org/freesoftware/artificial-intelligence.en.html)

### Análisis general
- [Navegando el código generado por IA: titularidad y responsabilidad -- MBHB](https://www.mbhb.com/intelligence/snippets/navigating-the-legal-landscape-of-ai-generated-code-ownership-and-liability-challenges/)
- [Contenido generado por IA y derecho de copyright -- Built In](https://builtin.com/artificial-intelligence/ai-copyright)
- [Inteligencia artificial y copyright -- Wikipedia](https://en.wikipedia.org/wiki/Artificial_intelligence_and_copyright)
- [Preocupaciones de copyright y mejores practicas en IA generativa (2026)](https://research.aimultiple.com/generative-ai-copyright/)
- [Cambios en la política de datos de entrenamiento de GitHub Copilot (marzo 2026) -- The Register](https://www.theregister.com/2026/03/26/github_ai_training_policy_changes/)

---

*Aviso legal: Este informe es un análisis de investigación y no constituye asesoramiento jurídico. Para decisiones con consecuencias legales o financieras significativas, consulte a un abogado especializado en propiedad intelectual en su jurisdicción.*
