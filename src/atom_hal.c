#include "atom.h"

typedef struct {
    int id;
    const char *name;
    const char *tag;
    int port_base;
    bool input;
} HalDevice;

typedef struct {
    int cursor;
    long last;
    int total;
} HalState;

static HalDevice s_devices[MAX_HAL_DEVICES] = {
    { 1, "SATA",      "INPHAL",  0x01F0, true  },
    { 2, "COM_UART",  "INPHAL",  0x03F8, true  },
    { 3, "USB",       "INPHAL",  0xE000, true  },
    { 4, "VGA",       "OUTHAL",  0x03C0, false },
    { 5, "PS2",       "INPHAL",  0x0060, true  },
    { 6, "HDMI",      "OUTHAL",  0x0600, false },
    { 7, "RJ45",      "INPHAL",  0xC000, true  },
    { 8, "AUDIO",     "INPHAL",  0x0388, true  },
    { 9, "LEGACY_RJ", "INPHAL",  0x02E8, true  },
    {10, "RJ9",       "INPHAL",  0x02E8, true  },
    {11, "RJ11",      "INPHAL",  0x02F8, true  },
    {12, "RJ14",      "INPHAL",  0x02E0, true  },
    {13, "RJ25",      "INPHAL",  0x02C8, true  },
    {14, "RESERVED",  "INPHAL",  0x0F00, true  }
};

static HalState s_state[MAX_HAL_DEVICES];

static HalDevice *device_of(int id) {
    if (id < 1 || id > MAX_HAL_DEVICES) return NULL;
    return &s_devices[id - 1];
}

int hal_device_count(void) {
    return MAX_HAL_DEVICES;
}

const char *hal_device_name(int id) {
    HalDevice *dev = device_of(id);
    return dev ? dev->name : "UNKNOWN";
}

int hal_window(int id) {
    if (id < 1 || id > MAX_HAL_DEVICES) return -1;
    return HAL_WINDOW_BASE + (id - 1) * HAL_WINDOW_SIZE;
}

void hal_init(AtomVM *vm) {
    memset(s_state, 0, sizeof(s_state));
    for (int i = 1; i <= MAX_HAL_DEVICES; i++) {
        int win = hal_window(i);
        if (win < 0 || win + HAL_BUF_COUNT >= MEMORY_SIZE) continue;
        vm->memory[win + HAL_BUF_COUNT] = 0;
        for (int k = 0; k < HAL_BUF_CAPACITY; k++) {
            vm->memory[win + HAL_BUF_DATA + k] = 0;
        }
    }
    for (int i = 0; i < MAX_PORTS; i++) {
        vm->port_values[i] = 0;
    }
}

void hal_reset(AtomVM *vm) {
    for (int i = 1; i <= MAX_HAL_DEVICES; i++) {
        int win = hal_window(i);
        if (win < 0 || win + HAL_BUF_COUNT >= MEMORY_SIZE) continue;
        vm->memory[win + HAL_BUF_COUNT] = 0;
        memset(&s_state[i - 1], 0, sizeof(HalState));
    }
}

bool hal_port_device(int port, int *device) {
    for (int i = 0; i < MAX_HAL_DEVICES; i++) {
        if (port >= s_devices[i].port_base && port < s_devices[i].port_base + 8) {
            *device = s_devices[i].id;
            return true;
        }
    }
    return false;
}

long hal_read(AtomVM *vm, int id) {
    HalDevice *dev = device_of(id);
    int win = hal_window(id);
    if (!dev || win < 0) return -1;

    if (!dev->input) {
        printf("[HAL G%d] %s (%s) has no input line\n", id, dev->name, dev->tag);
        return -1;
    }

    int count = vm->memory[win + HAL_BUF_COUNT];
    if (count > HAL_BUF_CAPACITY) count = HAL_BUF_CAPACITY;

    if (s_state[id - 1].cursor >= count) {
        s_state[id - 1].cursor = 0;
        printf("[HAL G%d] %s (%s) input buffer empty\n", id, dev->name, dev->tag);
        return -1;
    }

    long unit = vm->memory[win + HAL_BUF_DATA + s_state[id - 1].cursor];
    s_state[id - 1].cursor++;
    s_state[id - 1].last = unit;
    s_state[id - 1].total++;
    vm->memory[win + HAL_BUF_DATA + HAL_BUF_CAPACITY] = (unsigned char)s_state[id - 1].cursor;
    vm->memory[win + HAL_BUF_DATA + HAL_BUF_CAPACITY + 1] = (unsigned char)(unit & 0xFF);
    printf("[HAL G%d] %s (%s) -> 0x%02lX (%ld)\n", id, dev->name, dev->tag, unit, unit);
    return unit;
}

bool hal_write(AtomVM *vm, int id, long value) {
    HalDevice *dev = device_of(id);
    int win = hal_window(id);
    if (!dev || win < 0) return false;
    if (dev->input) {
        printf("[HAL W%d] %s (%s) is input only\n", id, dev->name, dev->tag);
        return false;
    }
    int count = vm->memory[win + HAL_BUF_COUNT];
    if (count >= HAL_BUF_CAPACITY) {
        printf("[HAL W%d] %s (%s) output buffer full\n", id, dev->name, dev->tag);
        return false;
    }
    vm->memory[win + HAL_BUF_DATA + count] = (unsigned char)(value & 0xFF);
    vm->memory[win + HAL_BUF_COUNT] = (unsigned char)(count + 1);
    printf("[HAL W%d] %s (%s) <- 0x%02lX\n", id, dev->name, dev->tag, value & 0xFF);
    return true;
}

uint32_t read_port(AtomVM *vm, int port) {
    if (port < 0 || port >= MAX_PORTS) return 0;
    int device = 0;
    if (hal_port_device(port, &device)) {
        return (uint32_t)hal_read(vm, device);
    }
    return vm->port_values[port];
}

void write_port(AtomVM *vm, int port, uint32_t value) {
    if (port < 0 || port >= MAX_PORTS) return;
    int device = 0;
    if (hal_port_device(port, &device)) {
        hal_write(vm, device, (long)value);
        return;
    }
    vm->port_values[port] = value;
    if (port == 1) {
        putchar((char)value);
    }
}
