# QtreGB — Plan: APU simple

Documento de implementación del sonido. El objetivo **no** es un APU exacto:
es entender la idea general y conseguir que el emulador saque sonido con una
arquitectura razonable y simplificada. Las imprecisiones quedan documentadas
en la sección de deudas técnicas.

> **Progreso (2026-11-16):** hitos 1–10 + DIV sync + savestates del APU
> completados, y `dmg_sound` en **12/12**. El APU existe en `core/inc/apu.h` +
> `core/src/apu.cpp` con el register file, el frame sequencer (cloqueado por el
> bit 12 del divisor), los cuatro canales, length, envelope, sweep de CH1,
> mezcla estéreo con `NR50`/`NR51`, HPF y `serialize()`. Produce PCM a 48 kHz
> en un ring buffer y la GUI lo envía a SDL. Queda el savestate completo de la
> máquina y quirks no cubiertos por `dmg_sound`.

Estado de partida original (2026-11-16):

- No existe APU. `FF10–FF3F` caen en `IO::io_read`, que devuelve `0xFF`, y en
  `IO::io_write`, que descarta la escritura.
- `gui/src/sdl_renderer_backend.cpp` inicializa `SDL_INIT_AUDIO`, pero no se
  abre ningún dispositivo ni se encola PCM.

---

## 0. Decisión: ¿dónde van las funciones de onda?

**En el core.** La GUI solo debe abrir el dispositivo de audio, consumir PCM y
aplicar el volumen del usuario.

```
┌──────────────────────────── core ────────────────────────────┐
│ APU: registros, contadores, duty/LFSR/wave RAM,              │
│      length/envelope/sweep, mezcla, resampling → PCM          │
└──────────────────────────────┬───────────────────────────────┘
                               │ int16 estéreo 48 kHz
┌──────────────────────────────▼───────────────────────────────┐
│ GUI: SDL_OpenAudioDevice + SDL_QueueAudio + volumen           │
└──────────────────────────────────────────────────────────────┘
```

Motivos:

1. **No basta con leer los registros.** La forma de onda no depende solo de
   `NRx3/NRx4`: depende de contadores internos que avanzan a 4.194.304 Hz
   (timer de frecuencia, paso de duty, índice de wave RAM, LFSR, length,
   envelope, sweep). Un frontend que solo leyera registros una vez por frame
   no podría avanzar el duty ni el LFSR en fase.
2. **Reutilización.** Cualquier consumidor del core (GUI, runners, tests)
   necesita el mismo comportamiento. Duplicarlo en cada frontend garantiza
   divergencias.
3. **Testeabilidad.** El APU se puede probar sin SDL: se le dan T-cycles y se
   comprueba el PCM generado.
4. **Separación de responsabilidades.** La GUI no debe saber qué es `NR43` ni
   cómo funciona un LFSR; el core no debe saber qué es un dispositivo SDL.

El "helper" que mencionas es, literalmente, la **API pública del APU**
(`core/inc/apu.h`): el core la exporta y cualquier consumidor la usa. No es
código de la GUI ni código duplicado.

La única parte que puede vivir en la GUI es el **remuestreo final al formato
del dispositivo** si se quiere soportar cualquier frecuencia de salida. Para
el MVP se fija 48 kHz en el core y la GUI no remuestrea.

---

## 1. Alcance del MVP

Incluido:

- 4 canales: 2 de pulso, 1 de onda, 1 de ruido.
- Registros `FF10–FF2F` + wave RAM `FF30–FF3F`.
- Trigger, length timer, envelope.
- Frame sequencer a 512 Hz.
- Mezcla estéreo con `NR50`/`NR51`.
- Salida PCM `int16` estéreo a 48 kHz con un ring buffer.
- Integración con `QtreGB` y con SDL.

Fuera del MVP (se puede añadir después):

- Quirks: zombie mode, corrupción de wave RAM, triggers exactos, `DIV` writes
  afectando al frame sequencer.
- VIN, `PCM12`/`PCM34` (CGB), doble velocidad.
- Savestate completo de la máquina (el APU ya serializa su propio estado).

---

## 2. Mapa de registros

### Globales

| Dir | Nombre | Contenido |
|---|---|---|
| `FF24` | NR50 | bit 7 VIN izq.; bits 6–4 volumen izq. (0–7); bit 3 VIN der.; bits 2–0 volumen der. |
| `FF25` | NR51 | bits 7–4: CH4–CH1 a la izquierda; bits 3–0: CH4–CH1 a la derecha |
| `FF26` | NR52 | bit 7 encendido del APU; bits 3–0 canales activos (solo lectura) |

### CH1 — pulso + sweep

| Dir | Nombre | Contenido |
|---|---|---|
| `FF10` | NR10 | bits 6–4 periodo de sweep; bit 3 dirección (1 resta); bits 2–0 shift |
| `FF11` | NR11 | bits 7–6 duty; bits 5–0 length |
| `FF12` | NR12 | bits 7–4 volumen inicial; bit 3 dirección envelope; bits 2–0 periodo |
| `FF13` | NR13 | frecuencia, bits 7–0 |
| `FF14` | NR14 | bit 7 trigger; bit 6 length enable; bits 2–0 frecuencia, bits 10–8 |

### CH2 — pulso

`FF16–FF19` (NR21–NR24), igual que CH1 sin sweep.

### CH3 — onda

| Dir | Nombre | Contenido |
|---|---|---|
| `FF1A` | NR30 | bit 7 DAC enable |
| `FF1B` | NR31 | length (8 bits; 0 = 256) |
| `FF1C` | NR32 | bits 6–5 nivel: 0 mute, 1 100 %, 2 50 %, 3 25 % |
| `FF1D` | NR33 | frecuencia, bits 7–0 |
| `FF1E` | NR34 | bit 7 trigger; bit 6 length enable; bits 2–0 frecuencia, bits 10–8 |
| `FF30–FF3F` | Wave RAM | 32 muestras de 4 bits (2 por byte) |

### CH4 — ruido

| Dir | Nombre | Contenido |
|---|---|---|
| `FF20` | NR41 | bits 5–0 length |
| `FF21` | NR42 | envelope, igual que NR12 |
| `FF22` | NR43 | bits 7–4 shift; bit 3 LFSR width (0 = 15 bits, 1 = 7 bits); bits 2–0 divisor |
| `FF23` | NR44 | bit 7 trigger; bit 6 length enable |

### Encendido

`NR52` bit 7:

- `0`: APU apagado. `FF10–FF25` se leen como 0 y las escrituras se ignoran.
  Al apagar: `enabled = false` en los cuatro canales y `m_frame_step = 0`.
- `1`: APU encendido. Al pasar de 0 a 1 se reinician los canales y el frame
  sequencer (`m_frame_counter = 0`, `m_frame_step = 0`) y se limpian los
  registros de canal. La wave RAM no hace falta limpiarla en el MVP.

En ambos casos, el byte escrito en `NR52` se conserva; los bits 3–0 de lectura
se calculan a partir de los canales activos.

---

## 3. Modelo de datos

Un solo fichero de cabecera y otro de implementación:

```
core/inc/apu.h
core/src/apu.cpp
```

Estructura sugerida (nombres y estilo del repo: `m_`, clase por fichero):

```cpp
namespace qtreGB
{

class APU
{
public:
    static constexpr u32 CLOCK = 4194304;      // Hz del reloj maestro
    static constexpr u32 SAMPLE_RATE = 48000;  // Hz de salida
    static constexpr size_t RING_FRAMES = 4096;

    void reset();
    void tick();                                // un T-cycle

    u8 read(u16 address);
    void write(u16 address, u8 value);

    size_t available_frames() const;
    size_t read_samples(int16_t* dst, size_t frames);

private:
    enum ChannelId { CH1, CH2, CH3, CH4 };

    struct Pulse
    {
        bool enabled = false;
        u8 duty = 0;                 // 0..3
        u8 duty_step = 0;            // 0..7
        u16 frequency = 0;           // 0..2047
        int timer = 0;               // T-cycles hasta el siguiente paso
        u8 volume = 0;               // 0..15
        u8 initial_volume = 0;
        u8 env_period = 0;
        u8 env_timer = 0;
        bool env_increase = false;
        u16 length = 0;              // restante (máx. 64)
        bool length_enable = false;
    };

    struct Wave
    {
        bool enabled = false;
        u16 frequency = 0;
        int timer = 0;
        u8 sample_index = 0;         // 0..31
        u8 sample = 0;               // 0..15
        u8 level = 0;                // NR32 bits 6-5
        u16 length = 0;              // restante (máx. 256)
        bool length_enable = false;
    };

    struct Noise
    {
        bool enabled = false;
        int timer = 0;
        u16 lfsr = 0x7FFF;
        bool short_mode = false;     // 7 bits en vez de 15
        u8 volume = 0;
        u8 initial_volume = 0;
        u8 env_period = 0;
        u8 env_timer = 0;
        bool env_increase = false;
        u16 length = 0;              // restante (máx. 64)
        bool length_enable = false;
        u8 clock_shift = 0;
        u8 divisor_code = 0;
    };

    // registros
    u8 m_nr10 = 0, m_nr11 = 0, m_nr12 = 0, m_nr13 = 0, m_nr14 = 0;
    u8 m_nr21 = 0, m_nr22 = 0, m_nr23 = 0, m_nr24 = 0;
    u8 m_nr30 = 0, m_nr31 = 0, m_nr32 = 0, m_nr33 = 0, m_nr34 = 0;
    u8 m_nr41 = 0, m_nr42 = 0, m_nr43 = 0, m_nr44 = 0;
    u8 m_nr50 = 0, m_nr51 = 0, m_nr52 = 0;
    u8 m_wave_ram[16] = {};

    bool m_powered = false;

    Pulse m_ch1, m_ch2;
    Wave  m_ch3;
    Noise m_ch4;

    // frame sequencer
    u32 m_frame_counter = 0;   // T-cycles
    u8  m_frame_step = 0;      // 0..7

    // resampling + ring buffer
    u32 m_sample_acc = 0;
    std::array<int16_t, RING_FRAMES * 2> m_ring{};
    size_t m_ring_read = 0;
    size_t m_ring_write = 0;
    size_t m_ring_count = 0;

    void tick_pulse(Pulse& ch);
    void tick_wave();
    void tick_noise();
    void clock_frame_sequencer();
    bool clock_length(u16& length, bool enable);   // false si expira
    void clock_envelope(u8& volume, u8& timer, u8 period, bool increase);
    // ...
    void push_sample(int16_t left, int16_t right);
    void mix_and_emit();
};

} // namespace qtreGB
```

Para el MVP se puede omitir el sweep y guardar solo lo necesario. Lo importante
es que el estado interno viva aquí, no en la GUI.

---

## 4. Reloj y frame sequencer

El APU avanza **una vez por T-cycle**. `QtreGB::tick()` ya es un T-cycle; el
sitio natural es `QtreGB::tick_once()`, junto a `m_ppu.ppu_tick()`.

El frame sequencer va a **512 Hz**, es decir, un paso cada `4194304 / 512 =
8192` T-cycles:

```cpp
void APU::tick()
{
    if(!m_powered)
    {
        return;
    }

    if(++m_frame_counter >= 8192)
    {
        m_frame_counter = 0;
        clock_frame_sequencer();
    }

    tick_pulse(m_ch1);   // same timer/duty path as CH2
    tick_pulse(m_ch2);   // sweep CH1 se clockea desde el frame sequencer
    tick_wave();
    tick_noise();

    // Resampling: 48000 muestras/s desde 4194304 T-cycles/s.
    m_sample_acc += SAMPLE_RATE;
    if(m_sample_acc >= CLOCK)
    {
        m_sample_acc -= CLOCK;
        mix_and_emit();
    }
}
```

Pasos del frame sequencer:

| Paso | Acción | Frecuencia efectiva |
|---|---|---|
| 0 | length de los cuatro canales | 256 Hz |
| 1 | — | |
| 2 | length de los cuatro + sweep CH1 | 256 / 128 Hz |
| 3 | — | |
| 4 | length de los cuatro | 256 Hz |
| 5 | — | |
| 6 | length de los cuatro + sweep CH1 | 256 / 128 Hz |
| 7 | envelope de CH1/CH2/CH4 | 64 Hz |

```cpp
void APU::clock_frame_sequencer()
{
    const bool length_step = (m_frame_step == 0 || m_frame_step == 2 ||
                              m_frame_step == 4 || m_frame_step == 6);
    if(length_step)
    {
        if(!clock_length(m_ch1.length, m_ch1.length_enable)) m_ch1.enabled = false;
        if(!clock_length(m_ch2.length, m_ch2.length_enable)) m_ch2.enabled = false;
        if(!clock_length(m_ch3.length, m_ch3.length_enable)) m_ch3.enabled = false;
        if(!clock_length(m_ch4.length, m_ch4.length_enable)) m_ch4.enabled = false;
    }

    if(m_frame_step == 2 || m_frame_step == 6)
    {
        // sweep(m_ch1) aquí, si se implementa
    }

    if(m_frame_step == 7)
    {
        clock_envelope(m_ch1.volume, m_ch1.env_timer, m_ch1.env_period, m_ch1.env_increase);
        clock_envelope(m_ch2.volume, m_ch2.env_timer, m_ch2.env_period, m_ch2.env_increase);
        clock_envelope(m_ch4.volume, m_ch4.env_timer, m_ch4.env_period, m_ch4.env_increase);
            break;
        default:
            break;
    }

    m_frame_step = static_cast<u8>((m_frame_step + 1) & 0x07);
}
```

### Sincronización con DIV (implementada)

En hardware, el frame sequencer se dispara con el flanco de bajada del **bit 12
del contador interno de 16 bits del divisor**, que es el **bit 4 del registro
`DIV` visible** (`FF04`), a 512 Hz.

`Timer` expone el divisor completo con `u16 read_div16() const`, y
`QtreGB::tick_once()` llama a `m_apu.tick(m_timer.read_div16())` después del
tick del timer en T3. El APU detecta el flanco y avanza el frame sequencer:

```cpp
void APU::tick(u16 div)
{
    if(!m_powered)
    {
        m_last_div = div;
        return;
    }

    if((m_last_div & 0x1000) != 0 && (div & 0x1000) == 0)
    {
        clock_frame_sequencer();
    }
    m_last_div = div;
    // ... canales y resampling
}
```

Esto también reproduce el tick extra cuando se escribe `DIV` con el bit 4 a 1
(al ponerlo a 0 se genera el flanco). Para los tests unitarios, `TestApu`
simula un divisor que incrementa 4 cada 4 T-cycles, igual que el timer real.

---

## 5. Canales

### 5.1 Pulso (CH1 y CH2)

El timer de frecuencia avanza el paso de duty:

```cpp
void APU::tick_pulse(Pulse& ch)
{
    if(!ch.enabled)
    {
        return;
    }

    if(ch.timer > 0)
    {
        --ch.timer;
    }
    if(ch.timer == 0)
    {
        ch.timer = (2048 - ch.frequency) * 4;   // T-cycles por paso
        ch.duty_step = static_cast<u8>((ch.duty_step + 1) & 0x07);
    }
}
```

Tabla de duty (1 = nivel alto):

```cpp
static constexpr u8 DUTY[4][8] = {
    {0, 0, 0, 0, 0, 0, 0, 1},   // 12.5 %
    {1, 0, 0, 0, 0, 0, 0, 1},   // 25 %
    {1, 0, 0, 0, 0, 1, 1, 1},   // 50 %
    {0, 1, 1, 1, 1, 1, 1, 0},   // 75 %
};
```

Salida digital del canal:

```cpp
u8 output = DUTY[ch.duty][ch.duty_step] ? ch.volume : 0;   // 0..15
```

Frecuencia resultante: `131072 / (2048 - frequency)` Hz.

CH1 y CH2 comparten exactamente esta lógica. La única diferencia de
generación es el **sweep de CH1**: no forma parte de `tick_pulse()` porque no
se clockea por T-cycle, sino desde el frame sequencer a 128 Hz (pasos 2 y 6).
Hasta que se implemente, `NR10` se guarda pero no tiene efecto.

### 5.2 Onda (CH3)

Timer por muestra: `(2048 - frequency) * 2` T-cycles (32 muestras por ciclo).

```cpp
void APU::tick_wave()
{
    if(!m_ch3.enabled)
    {
        return;
    }

    if(m_ch3.timer > 0)
    {
        --m_ch3.timer;
    }
    if(m_ch3.timer == 0)
    {
        m_ch3.timer = (2048 - m_ch3.frequency) * 2;
        m_ch3.sample_index = static_cast<u8>((m_ch3.sample_index + 1) & 0x1F);

        const u8 byte = m_wave_ram[m_ch3.sample_index >> 1];
        m_ch3.sample = (m_ch3.sample_index & 0x01) ? (byte & 0x0F) : (byte >> 4);
    }
}
```

Nivel (`NR32` bits 6–5): 0 → silencio, 1 → muestra completa, 2 → mitad,
3 → cuarto. En el MVP se puede aplicar como shift con la tabla `WAVE_SHIFT`
que se usa también en la mezcla (`{4, 0, 1, 2}`):

```cpp
u8 output = m_ch3.sample >> WAVE_SHIFT[m_ch3.level & 0x03];
```

### 5.3 Ruido (CH4)

Divisores de `NR43` (bits 2–0): `8, 16, 32, 48, 64, 80, 96, 112`.
Periodo del LFSR: `divisor << clock_shift` T-cycles.
El bit 3 de `NR43` selecciona el modo corto (LFSR de 7 bits en vez de 15).
Un `clock_shift` de 14 o 15 detiene el LFSR por completo.

```cpp
void APU::tick_noise()
{
    if(!m_ch4.enabled)
    {
        return;
    }

    if(m_ch4.timer > 0)
    {
        --m_ch4.timer;
    }
    if(m_ch4.timer == 0)
    {
        static constexpr u8 DIVISOR[8] = {8, 16, 32, 48, 64, 80, 96, 112};
        m_ch4.timer = DIVISOR[m_ch4.divisor_code & 0x07] << (m_ch4.clock_shift & 0x0F);

        const u16 bit = static_cast<u16>((m_ch4.lfsr ^ (m_ch4.lfsr >> 1)) & 0x01);
        m_ch4.lfsr = static_cast<u16>((m_ch4.lfsr >> 1) | (bit << 14));
        if(m_ch4.short_mode)
        {
            m_ch4.lfsr = static_cast<u16>((m_ch4.lfsr & ~0x0040) | (bit << 6));
        }
    }
}
```

Salida: `(m_ch4.lfsr & 0x01) ? m_ch4.volume : 0`.

### 5.4 Trigger, length y envelope

**Trigger** = escritura en `NRx4`/`NR44` con el bit 7 a 1:

- `enabled = true` (salvo DAC apagado).
- `timer = periodo`.
- CH3: `sample_index = 0`, `sample = 0`.
- CH4: `lfsr = 0x7FFF`.
- `volume = initial_volume`; `env_timer = env_period ? env_period : 8`.
- Si `length == 0`, recargar con el máximo (64 o 256).

**Length** (256 Hz): si `length_enable` y `length > 0`, decrementar; al llegar a
0, apagar el canal.

```cpp
bool APU::clock_length(u16& length, bool enable)
{
    if(enable && length > 0)
    {
        return --length != 0;   // false => el llamador apaga el canal
    }
    return true;
}
```

Al escribir `NRx1`/`NR31`, guardar `length = max_length - (value & mask)`.
Simplificación aceptable: guardar el valor y recargar en el trigger.

**Envelope** (64 Hz), solo CH1/CH2/CH4:

```cpp
void APU::clock_envelope(u8& volume, u8& timer, u8 period, bool increase)
{
    if(period == 0)
    {
        return;
    }
    if(--timer == 0)
    {
        timer = period;
        if(increase && volume < 15)
        {
            ++volume;
        }
        else if(!increase && volume > 0)
        {
            --volume;
        }
    }
}
```

**Sweep CH1** (128 Hz, pasos 2 y 6 del frame sequencer):

- En el trigger: `shadow = frequency`, `timer = pace` (0 se trata como 8),
  `enabled = pace != 0 || shift != 0`. Si `shift != 0`, se hace una
  comprobación inmediata de overflow.
- En cada tick de 128 Hz, si está habilitado y `pace != 0`: se decrementa el
  timer; al llegar a 0 se recarga y se calcula
  `nueva = shadow ± (shadow >> shift)`.
- Si `nueva > 2047`, CH1 se apaga. Si no y `shift != 0`, se actualizan
  `shadow`, la frecuencia y el timer del canal; después se hace una segunda
  comprobación de overflow que no se escribe de vuelta.

**DAC**: `NRx2 & 0xF8 != 0` (CH1/2/4) y `NR30 bit 7` (CH3). Si el DAC está
apagado, el canal se desactiva. No se modela la rampa analógica del DAC.

---

## 6. Mezcla y PCM

Cada canal produce `0..15`. En hardware, 0 se convierte en el valor analógico
más alto y 15 en el más bajo (el DAC es inversor), pero para el MVP basta con
una mezcla lineal con esa inversión para que las ondas no queden invertidas
respecto al hardware.

```cpp
void APU::mix_channel(Channel channel, int& left, int& right) const
{
    if(!is_channel_enabled(channel))
    {
        return;
    }

    // El DAC es inversor: 0 -> +1, 15 -> -1. Centramos en 0.
    const int value = 7 - static_cast<int>(channel_output(channel));
    if(m_nr51 & (1 << (4 + channel))) left += value;
    if(m_nr51 & (1 << channel))       right += value;
}

void APU::mix_and_emit()
{
    int left = 0;
    int right = 0;

    mix_channel(CH1, left, right);
    mix_channel(CH2, left, right);
    mix_channel(CH3, left, right);
    mix_channel(CH4, left, right);

    // NR50: 0 significa 1/8 y 7 significa 8/8.
    left  = left  * (((m_nr50 >> 4) & 0x07) + 1) / 8;
    right = right * ((m_nr50 & 0x07) + 1) / 8;

    const float l = high_pass(static_cast<float>(left * 1000), m_hpf_left);
    const float r = high_pass(static_cast<float>(right * 1000), m_hpf_right);

    push_sample(clamp_sample(static_cast<int>(l)),
                clamp_sample(static_cast<int>(r)));
}
```

Con la escala `1000`, el peor caso de los cuatro canales (±32) da ±32000,
dentro de `int16`; el `clamp` sigue protegiendo los transitorios del HPF.

### Filtro paso-alto (HPF)

El HPF elimina la componente continua que dejan los canales con DAC activo y
suaviza los pops al encender/apagar. Se aplica a 48 kHz con el coeficiente
equivalente al del DMG:

```cpp
constexpr float HPF_COEFFICIENT = 0.99634f;   // 0.999958^(4194304/48000)

float high_pass(float in, float& capacitor)
{
    const float out = in - capacitor;
    capacitor = in - out * HPF_COEFFICIENT;
    return out;
}
```

Los capacitores (`m_hpf_left`, `m_hpf_right`) se resetean en `reset()` y al
apagar el APU.

### Ring buffer

```cpp
void APU::push_sample(int16_t left, int16_t right)
{
    if(m_ring_count == RING_FRAMES)
    {
        // buffer lleno: descartar la muestra más antigua
        m_ring_read = (m_ring_read + 1) % RING_FRAMES;
        --m_ring_count;
    }

    m_ring[m_ring_write * 2 + 0] = left;
    m_ring[m_ring_write * 2 + 1] = right;
    m_ring_write = (m_ring_write + 1) % RING_FRAMES;
    ++m_ring_count;
}

size_t APU::available_frames() const
{
    return m_ring_count;
}

size_t APU::read_samples(int16_t* dst, size_t frames)
{
    const size_t n = std::min(frames, m_ring_count);
    for(size_t i = 0; i < n; ++i)
    {
        dst[i * 2 + 0] = m_ring[m_ring_read * 2 + 0];
        dst[i * 2 + 1] = m_ring[m_ring_read * 2 + 1];
        m_ring_read = (m_ring_read + 1) % RING_FRAMES;
    }
    m_ring_count -= n;
    return n;
}
```

`RING_FRAMES = 4096` cubre unos 85 ms a 48 kHz, suficiente para un frame
(≈800 muestras) con margen.

### Savestates del APU

`APU::serialize(TaggedSerializer&)` guarda:

- registros `NR10–NR51`, power, wave RAM;
- estado de los cuatro canales (timers, duty, LFSR, length, envelope, sweep);
- `m_last_div`, `m_frame_step`, `m_apu_phase`, `m_cycle`;
- fase del resampler (`m_sample_acc`) y capacitores del HPF.

No guarda el **ring buffer PCM**: es una cola de salida del host, no estado de
la máquina. Al leer un state se vacía el ring y se resetea la ventana
transitoria de acceso a wave RAM. Cuando exista savestate completo de máquina,
el frontend debe limpiar también la cola de SDL al cargar.

---

## 7. Integración en el core

### 7.1 Ficheros

```
core/inc/apu.h
core/src/apu.cpp
```

El `Makefile` del core usa `wildcard`, así que no hay que añadir nada.

### 7.2 `Bus`

Añadir el puntero y el despacho de registros:

```cpp
// bus.h
#include "apu.h"

Bus(..., PPU* ppu, APU* apu, ICPURegisters* cpu_registers);

APU* m_apu;
```

```cpp
// bus.cpp, en read()
else if(address >= 0xFF10 && address <= 0xFF3F)
{
    return m_apu->read(address);
}
```

```cpp
// bus.cpp, en write()
else if(address >= 0xFF10 && address <= 0xFF3F)
{
    m_apu->write(address, value);
}
```

Colocar estas ramas **antes** del fallback a `m_io->io_read`/`io_write`, y no
solapar con `FF40–FF4B`, que ya están gestionados por la PPU.

### 7.3 `QtreGB`

```cpp
// QtreGB.h
#include "apu.h"

Timer m_timer;
APU m_apu;          // antes de m_bus
LCD m_lcd;
PPU m_ppu;
```

```cpp
// QtreGB.cpp, constructor
m_timer(Timer()),
m_apu(),
m_lcd(LCD()),
...
m_bus(Bus(&m_cart, &m_timer, &m_dma, &m_gamepad, &m_wram, &m_hram,
          &m_io, &m_lcd, &m_ppu, &m_apu, static_cast<ICPURegisters*>(&m_cpu))),
```

```cpp
// QtreGB::reset_machine()
m_apu.reset();
```

```cpp
// QtreGB::tick_once(), junto a la PPU
m_ppu.ppu_tick();
m_apu.tick();
```

Orden recomendado: CPU → timer → PPU → APU. El APU no depende de la PPU; solo
necesita avanzar una vez por T-cycle.

### 7.4 API pública

`QtreGB` expone sus miembros como públicos, así que la GUI puede hacer
`emu.m_apu.available_frames()` y `emu.m_apu.read_samples(...)` sin cambios
adicionales. Si se quiere encapsular, añadir:

```cpp
APU& get_apu() { return m_apu; }
```

---

## 8. Integración con la GUI (SDL)

La GUI solo necesita tres cosas: abrir el dispositivo, vaciar el ring buffer y
cerrarlo.

### 8.1 Abrir el dispositivo

En `EmulatorInstance` (tiene acceso al emulador y ya usa SDL):

```cpp
// gui/inc/emulator_instance.h
SDL_AudioDeviceID m_audio_device = 0;
void drain_audio();
```

```cpp
// gui/src/emulator_instance.cpp
bool EmulatorInstance::init_audio()
{
    SDL_AudioSpec want{};
    want.freq = 48000;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = nullptr;            // usamos SDL_QueueAudio

    m_audio_device = SDL_OpenAudioDevice(nullptr, 0, &want, nullptr, 0);
    if(m_audio_device == 0)
    {
        return false;
    }

    SDL_PauseAudioDevice(m_audio_device, 0);
    return true;
}
```

Llamar a `init_audio()` al cargar la ROM (o al final del constructor, cuando
`m_emulator` ya existe) y cerrar en el destructor con
`SDL_CloseAudioDevice(m_audio_device)`.

### 8.2 Vaciar el ring buffer

```cpp
void EmulatorInstance::drain_audio()
{
    if(m_audio_device == 0 || !m_is_emulator_ready)
    {
        return;
    }

    int16_t buffer[2048];   // 1024 frames estéreo
    const size_t frames = m_emulator->m_apu.read_samples(buffer, 1024);
    if(frames == 0)
    {
        return;
    }

    SDL_QueueAudio(m_audio_device, buffer,
                   static_cast<Uint32>(frames * 2 * sizeof(int16_t)));
}
```

Llamarlo **una vez por frame emulado**, no una vez por T-cycle, para no hacer
miles de `SDL_QueueAudio` por frame. En `gui/src/main.cpp`:

```cpp
if(frame_ready)
{
    frontend.get_emulator()->update_frame(GB_PALETTE);
    frontend.get_emulator()->drain_audio();
}
```

Un frame son ~800 muestras; el ring de 4096 da margen. Si
`SDL_GetQueuedAudioSize()` crece demasiado (el emulador va más rápido que el
reloj de audio), se puede limitar: si hay más de ~100 ms encolados, no encolar
más y dejar que se vacíe.

### 8.3 Volumen y pausa

- Volumen del usuario: `SDL_MixAudioFormat` o multiplicar las muestras antes de
  encolar. Para el MVP, ni siquiera hace falta.
- Pausa: `SDL_PauseAudioDevice(dev, 1)` al pausar la emulación.

---

## 9. Orden de implementación

Cada hito debería dejar el árbol compilando y con las suites verdes.

- [x] **1. Esqueleto del APU**: `apu.h/cpp`, registros, `NR52` power, `reset()`,
      `read/write` con máscaras básicas. Sin sonido todavía. *(hecho 2026-11-16)*
- [x] **2. Cableado en el core**: `Bus`, `QtreGB`, `reset_machine()`,
      `m_apu.tick()` por T-cycle. Tests de registros: escribir `NR52=0x80` y
      comprobar lecturas. *(hecho 2026-11-16)*
- [x] **3. CH1 de pulso**: timer, duty, volumen fijo (sin envelope). Es el
      primer hito en el que se puede ver una muestra distinta de cero.
      *(hecho 2026-11-16; la salida digital se consulta con
      `APU::channel_output(APU::CH1)` hasta que exista el mezclador)*
- [x] **4. Mezcla + resampling + ring buffer**: `available_frames()` y
      `read_samples()` empiezan a devolver datos. *(provisional 2026-11-16:
      solo CH1, sin `NR50`/`NR51`; el mezclador completo llega con los demás
      canales)*
- [x] **5. Salida SDL**: abrir dispositivo, `drain_audio()` por frame. Aquí ya
      se oye el primer pitido. *(provisional 2026-11-16: `SDL_QueueAudio`,
      48 kHz, `S16SYS` estéreo)*
- [x] **6. CH2**: copiar CH1 sin sweep, y mezclar CH1+CH2 con `NR50`/`NR51`.
      *(hecho 2026-11-16)*
- [x] **7. CH3 (wave RAM)**: 32 muestras, nivel `NR32`. *(hecho 2026-11-16)*
- [x] **8. CH4 (ruido)**: LFSR de 15/7 bits, divisor y shift.
      *(hecho 2026-11-16)*
- [x] **9. Length + envelope**: length en los cuatro canales y envelope en
      CH1/CH2/CH4. *(hecho 2026-11-16)*
- [x] **10. Opcional**: sweep de CH1 y HPF. *(hecho 2026-11-16; DIV sync y
      savestates del APU también hechos)*

Sugerencia: no empezar por el frame sequencer. El primer sonido sale con los
tres primeros hitos; length/envelope/sweep solo hacen que se parezca más al
hardware.

---

## 10. Pruebas

### Unitarias (doctest)

Añadir `tests/test_apu.cpp`:

- `NR52` apagado: `FF10–FF25` leen 0 y las escrituras no cambian nada.
- `NR52` encendido: los canales se activan al hacer trigger.
- CH1: trigger con `NR13=0x00`, `NR14=0x87`; avanzar `N` T-cycles; comprobar
  que `available_frames() > 0` y que hay muestras distintas de cero.
- CH3: escribir un patrón en `FF30–FF3F` y comprobar que la salida lo sigue.
- CH4: el LFSR cambia de estado tras suficientes ticks.
- `read_samples()` devuelve como mucho lo disponible y avanza el ring.

### ROMs

- `roms/blargg/dmg_sound/dmg_sound.gb`: el test completo de Blargg. Con un APU
  simplificado fallarán varios (quirks), pero sirve como diagnóstico y como
  lista de lo que falta. No es un gate del MVP.
- Cualquier ROM comercial con música: la prueba definitiva de que "suena".
- Test manual mínimo: un ROM que escriba `NR52=0x80`, `NR51=0xFF`,
  `NR50=0x77`, `NR11=0x80`, `NR12=0xF0`, `NR13=0x00`, `NR14=0x87` y haga un
  bucle infinito. Debe sonar un tono continuo.

### Criterio de "hecho" del MVP

- El emulador arranca, carga una ROM con sonido y se oye.
- No hay crashes ni bucles infinitos al encender/apagar `NR52`.
- Las suites existentes siguen verdes: doctest, blargg, mts, PPU acceptance y
  harness (el APU no debe afectar al timing de CPU/PPU).

---

## 11. Simplificaciones conscientes (deuda técnica)

| Simplificación | Efecto | Mejora posterior |
|---|---|---|
| Resampling nearest-neighbor | Aliasing en frecuencias altas | Filtro paso-bajo o blip-buffer |
| Quirks no cubiertos por `dmg_sound` | Comportamiento raro en casos límite | Implementar por fases (zombie mode, etc.) |
| Sin rampa analógica del DAC | Corte seco al apagar el DAC | Modelar el fade del DAC |
| Savestate completo de máquina | El APU serializa su estado, pero PPU/LCD/cart/DMA aún no | Añadir el resto de componentes |

Nada de esto impide que el emulador suene; solo lo aleja del hardware exacto.

---

## 12. Referencias

- Pan Docs — Audio: <https://gbdev.io/pandocs/Audio.html>
- Pan Docs — Audio Details: <https://gbdev.io/pandocs/Audio_details.html>
- Blargg `dmg_sound`: `roms/blargg/dmg_sound/dmg_sound.gb`
- Código existente que sirve de patrón:
  - `core/inc/timer.h` + `core/src/timer.cpp` (contadores por T-cycle),
  - `core/inc/ppu.h` + `core/src/ppu.cpp` (máquina de estados por dot),
  - `core/inc/QtreGB.h` + `core/src/QtreGB.cpp` (cableado de la máquina).

---

## 13. Baseline `dmg_sound` (2026-11-16)

Con el DIV sync implementado y `BLARGG_MAX_T=1000000000`, el multi-ROM ya
termina. Resultado actual:

```
01:ok  02:ok  03:ok  04:ok  05:ok  06:ok  07:ok  08:ok  09:ok  10:ok  11:ok  12:ok
```

**12/12.** El multi-ROM termina en `Passed`.

El modelo final de CH3 y wave RAM:

- CH3 samplea en T-cycles impares y tiene `trigger_delay = 3` ticks.
- La lectura de wave RAM con CH3 activo solo es válida con una edad de
  **2–3 T-cycles** desde el último sample; devuelve el byte muestreado.
- Fuera de esa ventana, las lecturas devuelven `0xFF` y las escrituras se
  ignoran.
- Las escrituras dentro de la ventana van al byte que está leyendo CH3.
- Al retrigger con CH3 a punto de samplear se corrompen los primeros 4 bytes.

Cambios de esta ronda:

- `NR10` se relee al escribir, period 0 como 8, negate mode, writeback de la
  nueva frecuencia a `NR13/NR14` y enabled solo en trigger.
- Length: clock extra, max−1 y counters preservados al apagar/encender.
- Wave: sample en fases impares, trigger delay 3, ventana de lectura por edad
  y corrupción al retrigger.

Los tests individuales están en `roms/blargg/dmg_sound/rom_singles/`.
