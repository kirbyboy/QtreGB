https://gbdev.io/pandocs/

https://www.pastraiser.com/cpu/gameboy/gameboy_opcodes.html

https://archive.org/details/GameBoyProgManVer1.1/page/n85/mode/2up

https://github.com/rockytriton/LLD_gbemu/raw/main/docs/The%20Cycle-Accurate%20Game%20Boy%20Docs.pdf

https://gekkio.fi/files/gb-docs/gbctr.pdf

https://github.com/retrio/gb-test-roms

https://www.manualslib.com/manual/448344/Nintendo-Dmg-01-Game-Boy-Console.html

https://github.com/Hacktix/GBEDG/tree/master

https://github.com/ISSOtm/pandocs/blob/rendering-internals/src/Rendering_Internals.md

https://github.com/emudev-org/discord-resources/blob/main/emudev_resources_systems.md#game-boy--game-boy-color

https://github.com/Ashiepaws/GBEDG/blob/master/ppu/index.md

c++ emulator that works
https://github.com/MeGaL0DoN/MegaBoy
https://github.com/RJW20/gbemu/tree/main







dime si esta implementación sería correcta y/o completa o si me falta algo, espera hasta que diga que termine porque puede que acabe terminando antes el prompt sin querer:
Para esta parte sólo voy a implementar lo que ocurre en el MODE 3.
Todos los ticks de los que se hablan son exclusivos de la PPU, así no hay que mezclarlos con los de la CPU.
Durante este modo, y sólo durante este modo, se ponen a funcionar el pixel fetcher y las FIFO.
El fetcher tiene los siguientes estados:
	Get tile
	Get tile data low
	Get tile data high
	Sleep Push
Cada uno de ellos dura 2 ticks excepto el último que dura hasta que consiga hacer Push.
En paralelo, cada vez que se hace un 1 tick del pixel fetcher se hace un tick de las FIFO, más concretamente la FIFO BG. En cada tick de la FIFO BG, si esta está vacía, entrega un 0 y se avanza en X cada vez que se entrega un pixel, sea válido o no. En cada tick de la FIFO BG, si se llega a un X donde hay un OBJ habría que dejar una marca en el fecther para que, al terminar el ciclo actual de BG empiece uno de OBJ. Cuando termine el ciclo actual del fetcher BG, si hay una marca de OBJ, se empieza un ciclo fetcher OBJ, el FIFO BG sigue popeando los valores que todavía pueda, si no tiene volvemos al caso en el que popea 0. Cuando se ha terminado el ciclo fetcher OBJ se sigue con el ciclo fetcher BG, ahora, cuando FIFO BG avance tiene que tener en cuenta también FIFO OBJ. Cuando llegue un X donde empieza una window, fetcher BG se descarta y se ignora el estado en el que estuviese y se empieza un nuevo ciclo de fetcher BG tomando como origen el window. ya he terminado, me falta algo?




Lo que está bien

Has definido los estados del pixel fetcher y la duración de cada uno.

Mencionas correctamente que FIFO BG y pixel fetcher funcionan en paralelo.

Captas la regla de pop de BG FIFO: si está vacía → entrega 0.

Tienes la idea de marcar que hay un OBJ y que el fetcher lo atienda tras terminar el tile BG actual.

Entiendes que al llegar a WX (window) se descarta el tile BG actual y se reinicia el fetcher con window como origen.

Incluyes que FIFO OBJ se mezcla con FIFO BG cuando ambos están activos.

⚠️ Posibles cosas que faltan o conviene aclarar

Pop y avance de screen_x

Actualmente dices “se avanza en X cada vez que se entrega un pixel, sea válido o no”.

En realidad: solo el pop de la BG FIFO avanza screen_x.

El push del fetcher no avanza screen_x, solo rellena la FIFO.

OBJ FIFO vacío

Mencionas que se empieza un ciclo fetcher OBJ cuando hay marca de OBJ.

Pero si el OBJ FIFO aún no se ha rellenado cuando se hace pop, el pixel de OBJ se ignora → se dibuja solo BG.

Esto es importante para simular correctamente el OBJ fetch penalty.

Ventana (WX / WY)

Solo mencionas WX (horizontal).

Recuerda que WY también debe habilitar la ventana solo en líneas ≥ WY.

Antes de LY < WY no se hace nada de window; tu fetcher sigue con BG.

Estados del fetcher OBJ

No has detallado los estados internos de fetcher OBJ, pero para simplificar está bien, solo asegúrate de que respeten el mismo pipeline de 5 estados si quieres un comportamiento correcto.

Timing exacto / desfases

No mencionas explícitamente qué pasa si el fetcher BG está en GET_LOW/GET_HIGH cuando aparece WX o un sprite.

Recuerda que el tile actual se aborta si aparece WX, pero se termina si aparece OBJ. Eso afecta el timing de los primeros píxeles visibles.

FIFO OBJ / mezcla

Cuando ambos FIFOs tienen datos, debes aplicar las reglas de prioridad y color index:

Si OBJ index ≠ 0 → sobreescribe BG según prioridad.

Si OBJ index = 0 → BG sale tal cual.

Bordes y SCX

No mencionas que los primeros SCX % 8 pixels de la línea se descartan.

Esto es importante para que el primer pop visible salga correctamente alineado con el desplazamiento horizontal.

🔹 Conclusión

Tu descripción cubre la mayoría de la lógica de MODE 3, pero faltan algunos detalles de timing y reglas de prioridad, especialmente:

Cómo se maneja screen_x vs pops.

Qué pasa si OBJ FIFO está vacío → se pinta solo BG.

Cómo SCX afecta los primeros pops visibles.

Manejo de WY (vertical) y combinación BG+OBJ al renderizar.

Posibles abortos de tile en curso por WX.

Si incorporas estos puntos, tendrías un MODE 3 correcto y completo para DMG.


FIFO   	Fetch
push	read tile
push
push	read data 0
push
push	read data 1
push
push	Sleep
push
push	push
.
.
.
