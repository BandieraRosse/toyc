#include "rasterfall_prop.h"
#include "math.h"
#include "string.h"
#include "toy_platform.h"
#include "tlibc_everything.h"

/* GLB remains metres, glb2rmesh stores 232 units per metre, and the world
 * uses 512 RFU per metre.  The conversion is deliberately kept here in the
 * asset profile rather than entering map syntax or gameplay state. */
#define RASTERFALL_PROP_RENDER_SCALE_MILLI \
    ((RASTERFALL_RFU_PER_METER * 1000 + 116) / 232)

static const struct rasterfall_prop_asset_profile prop_assets[] = {
    { RASTERFALL_PROP_ASSET_CRATE, "crate",
      "rasterfall/assets/models/props/industrial/rf_crate.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 614, 512, 512 } },
    { RASTERFALL_PROP_ASSET_BARRIER, "barrier",
      "rasterfall/assets/models/props/industrial/rf_barrier.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 1229, 512, 410 } },
    { RASTERFALL_PROP_ASSET_LAMP_POST, "lamp_post",
      "rasterfall/assets/models/props/industrial/rf_lamp_post.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 410, 1638, 410 } },
    { RASTERFALL_PROP_ASSET_SHORT_WALL, "short_wall",
      "rasterfall/assets/models/props/industrial/rf_short_wall.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 1229, 717, 256 } },
    { RASTERFALL_PROP_ASSET_RAILING, "railing",
      "rasterfall/assets/models/props/industrial/rf_railing.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 1229, 563, 154 } },
    { RASTERFALL_PROP_ASSET_VENT_UNIT, "vent_unit",
      "rasterfall/assets/models/props/industrial/rf_vent_unit.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 717, 614, 461 } },
    { RASTERFALL_PROP_ASSET_WORKBENCH, "workbench",
      "rasterfall/assets/models/props/industrial/rf_workbench.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 922, 461, 410 } },
    { RASTERFALL_PROP_ASSET_AMMO_CONTAINER, "ammo_container",
      "rasterfall/assets/models/props/industrial/rf_ammo_container.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 461, 307, 256 } },
    { RASTERFALL_PROP_ASSET_INDUSTRIAL_PILLAR, "industrial_pillar",
      "rasterfall/assets/models/props/industrial/rf_industrial_pillar.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 410, 1434, 410 } },
    { RASTERFALL_PROP_ASSET_PIPE_MODULE, "pipe_module",
      "rasterfall/assets/models/props/industrial/rf_pipe_module.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 819, 717, 410 } },
    { RASTERFALL_PROP_ASSET_POWER_UNIT, "power_unit",
      "rasterfall/assets/models/props/industrial/rf_power_unit.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 2458, 1331, 1229 } },
    { RASTERFALL_PROP_ASSET_GATE_FRAME, "gate_frame",
      "rasterfall/assets/models/props/industrial/rf_gate_frame.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 3072, 2150, 410 } },
    { RASTERFALL_PROP_ASSET_CONTROL_CABINET, "control_cabinet",
      "rasterfall/assets/models/props/industrial/rf_control_cabinet.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 614, 922, 307 } },
    { RASTERFALL_PROP_ASSET_ARCH_BEAM, "arch_beam",
      "rasterfall/assets/models/props/industrial/rf_arch_beam.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 3072, 307, 256 } },
    { RASTERFALL_PROP_ASSET_ARCH_SUPPORT, "arch_support",
      "rasterfall/assets/models/props/industrial/rf_arch_support.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 1229, 1434, 410 } },
    { RASTERFALL_PROP_ASSET_ARCH_WALL, "arch_wall",
      "rasterfall/assets/models/props/industrial/rf_arch_wall.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 2048, 2150, 123 } },
    { RASTERFALL_PROP_ASSET_ARCH_DOORWAY, "arch_doorway",
      "rasterfall/assets/models/props/industrial/rf_arch_doorway.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 3072, 2150, 154 } },
    { RASTERFALL_PROP_ASSET_ARCH_PIPE_STRAIGHT, "arch_pipe_straight",
      "rasterfall/assets/models/props/industrial/rf_arch_pipe_straight.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 1024, 317, 317 } },
    { RASTERFALL_PROP_ASSET_ARCH_PIPE_ELBOW, "arch_pipe_elbow",
      "rasterfall/assets/models/props/industrial/rf_arch_pipe_elbow.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 671, 317, 671 } },
    { RASTERFALL_PROP_ASSET_ARCH_PIPE_TEE, "arch_pipe_tee",
      "rasterfall/assets/models/props/industrial/rf_arch_pipe_tee.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 1024, 317, 671 } },
    { RASTERFALL_PROP_ASSET_ARCH_SERVICE_PANEL, "arch_service_panel",
      "rasterfall/assets/models/props/industrial/rf_arch_service_panel.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 819, 614, 82 } },
    { RASTERFALL_PROP_ASSET_ARCH_CABLE_TRAY, "arch_cable_tray",
      "rasterfall/assets/models/props/industrial/rf_arch_cable_tray.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 2048, 164, 123 } },
    { RASTERFALL_PROP_ASSET_ARCH_FLOOR_HATCH, "arch_floor_hatch",
      "rasterfall/assets/models/props/industrial/rf_arch_floor_hatch.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 819, 20, 614 } },
    { RASTERFALL_PROP_ASSET_CAMPUS_WALL_PLAIN, "campus_wall_plain",
      "rasterfall/assets/models/props/campus/rf_campus_wall_plain.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 0, 0, 0 } },
    { RASTERFALL_PROP_ASSET_CAMPUS_WALL_WINDOW, "campus_wall_window",
      "rasterfall/assets/models/props/campus/rf_campus_wall_window.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 0, 0, 0 } },
    { RASTERFALL_PROP_ASSET_CAMPUS_WINDOW_STRIP, "campus_window_strip",
      "rasterfall/assets/models/props/campus/rf_campus_window_strip.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 0, 0, 0 } },
    { RASTERFALL_PROP_ASSET_CAMPUS_ENTRANCE, "campus_entrance",
      "rasterfall/assets/models/props/campus/rf_campus_entrance.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 0, 0, 0 } },
    { RASTERFALL_PROP_ASSET_CAMPUS_ROOF_EDGE, "campus_roof_edge",
      "rasterfall/assets/models/props/campus/rf_campus_roof_edge.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 0, 0, 0 } },
    { RASTERFALL_PROP_ASSET_CAMPUS_COLUMN, "campus_column",
      "rasterfall/assets/models/props/campus/rf_campus_column.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 0, 0, 0 } },
    { RASTERFALL_PROP_ASSET_CAMPUS_STAIR_SHORT, "campus_stair_short",
      "rasterfall/assets/models/props/campus/rf_campus_stair_short.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 0, 0, 0 } },
    { RASTERFALL_PROP_ASSET_CAMPUS_STAIR_LONG, "campus_stair_long",
      "rasterfall/assets/models/props/campus/rf_campus_stair_long.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 0, 0, 0 } },
    { RASTERFALL_PROP_ASSET_CAMPUS_RETAINING_WALL, "campus_retaining_wall",
      "rasterfall/assets/models/props/campus/rf_campus_retaining_wall.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 0, 0, 0 } },
    { RASTERFALL_PROP_ASSET_CAMPUS_CURB, "campus_curb",
      "rasterfall/assets/models/props/campus/rf_campus_curb.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 0, 0, 0 } },
    { RASTERFALL_PROP_ASSET_CAMPUS_SIDEWALK, "campus_sidewalk",
      "rasterfall/assets/models/props/campus/rf_campus_sidewalk.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 0, 0, 0 } },
    { RASTERFALL_PROP_ASSET_CAMPUS_TREE_PROXY, "campus_tree_proxy",
      "rasterfall/assets/models/props/campus/rf_campus_tree_proxy.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 0, 0, 0 } },
    { RASTERFALL_PROP_ASSET_BOUNDARY_WALL, "boundary_wall",
      "", RASTERFALL_PROP_RENDER_SCALE_MILLI, { 0, 0, 0 } },
    { RASTERFALL_PROP_ASSET_FACILITY_DESK, "facility_desk",
      "rasterfall/assets/models/props/industrial/rf_facility_desk.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 922, 384, 384 } },
    { RASTERFALL_PROP_ASSET_FACILITY_CHAIR, "facility_chair",
      "rasterfall/assets/models/props/industrial/rf_facility_chair.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 307, 512, 307 } },
    { RASTERFALL_PROP_ASSET_FACILITY_MONITOR, "facility_monitor",
      "rasterfall/assets/models/props/industrial/rf_facility_monitor.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 333, 230, 77 } },
    { RASTERFALL_PROP_ASSET_FACILITY_COMMAND_TABLE, "facility_command_table",
      "rasterfall/assets/models/props/industrial/rf_facility_command_table.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 2048, 461, 1229 } },
    { RASTERFALL_PROP_ASSET_FACILITY_LOW_CABINET, "facility_low_cabinet",
      "rasterfall/assets/models/props/industrial/rf_facility_low_cabinet.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 614, 410, 230 } },
    { RASTERFALL_PROP_ASSET_FACILITY_BENCH, "facility_bench",
      "rasterfall/assets/models/props/industrial/rf_facility_bench.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 922, 256, 282 } },
    { RASTERFALL_PROP_ASSET_FACILITY_TERMINAL, "facility_terminal",
      "rasterfall/assets/models/props/industrial/rf_facility_terminal.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 512, 768, 307 } },

    { RASTERFALL_PROP_ASSET_HOST_RACK_FRAME, "host_rack_frame",
      "rasterfall/assets/models/props/host/rf_host_rack_frame.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 410, 1126, 543 } },
    { RASTERFALL_PROP_ASSET_HOST_BLANK_PANEL, "host_blank_panel",
      "rasterfall/assets/models/props/host/rf_host_blank_panel.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 358, 133, 512 } },
    { RASTERFALL_PROP_ASSET_HOST_CPU_MODULE, "host_cpu_module",
      "rasterfall/assets/models/props/host/rf_host_cpu_module.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 358, 133, 512 } },
    { RASTERFALL_PROP_ASSET_HOST_MEMORY_MODULE, "host_memory_module",
      "rasterfall/assets/models/props/host/rf_host_memory_module.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 358, 133, 512 } },
    { RASTERFALL_PROP_ASSET_HOST_RACK_FAN_PANEL, "host_rack_fan_panel",
      "rasterfall/assets/models/props/host/rf_host_rack_fan_panel.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 358, 154, 512 } },
    { RASTERFALL_PROP_ASSET_HOST_POWER_BUNDLE, "host_power_bundle",
      "rasterfall/assets/models/props/host/rf_host_power_bundle.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 41, 563, 307 } },
    { RASTERFALL_PROP_ASSET_HOST_DATA_BUNDLE, "host_data_bundle",
      "rasterfall/assets/models/props/host/rf_host_data_bundle.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 26, 563, 307 } },
    { RASTERFALL_PROP_ASSET_HOST_CPU_HEADER, "host_cpu_header",
      "rasterfall/assets/models/props/host/rf_host_cpu_header.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 358, 87, 512 } },
    { RASTERFALL_PROP_ASSET_HOST_MEMORY_HEADER, "host_memory_header",
      "rasterfall/assets/models/props/host/rf_host_memory_header.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 358, 87, 512 } },

    { RASTERFALL_PROP_ASSET_RESEARCH_COMPUTE_RACK, "research_compute_rack",
      "rasterfall/assets/models/props/research/rf_research_compute_rack.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 512, 1075, 461 } },
    { RASTERFALL_PROP_ASSET_RESEARCH_BUILD_RACK, "research_build_rack",
      "rasterfall/assets/models/props/research/rf_research_build_rack.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 512, 1075, 461 } },
    { RASTERFALL_PROP_ASSET_RESEARCH_POWER_COOLING, "research_power_cooling",
      "rasterfall/assets/models/props/research/rf_research_power_cooling.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 333, 947, 461 } },
    { RASTERFALL_PROP_ASSET_RESEARCH_TERMINAL, "research_terminal",
      "rasterfall/assets/models/props/research/rf_research_terminal.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 589, 922, 435 } },
    { RASTERFALL_PROP_ASSET_CORE_ANALYSIS_STATION, "core_analysis_station",
      "rasterfall/assets/models/props/research/rf_core_analysis_station.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 1229, 998, 589 } },
    { RASTERFALL_PROP_ASSET_RESEARCH_PROTOTYPE_BENCH, "research_prototype_bench",
      "rasterfall/assets/models/props/research/rf_research_prototype_bench.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 1075, 819, 512 } },
    { RASTERFALL_PROP_ASSET_RESEARCH_STATUS_PANEL, "research_status_panel",
      "rasterfall/assets/models/props/research/rf_research_status_panel.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 640, 410, 92 } },
    { RASTERFALL_PROP_ASSET_RESEARCH_WALL_SERVICE, "research_wall_service",
      "rasterfall/assets/models/props/research/rf_research_wall_service.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 922, 333, 113 } },

    { RASTERFALL_PROP_ASSET_LAB_COMPUTER_STAND, "lab_computer_stand",
      "rasterfall/assets/models/props/lab/rf_lab_computer_stand.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 920, 1040, 500 } },
    { RASTERFALL_PROP_ASSET_LAB_COMPUTER_CASE, "lab_computer_case",
      "rasterfall/assets/models/props/lab/rf_lab_computer_case.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 0, 0, 0 } },
    { RASTERFALL_PROP_ASSET_LAB_COMPUTER_BOARD, "lab_computer_board",
      "rasterfall/assets/models/props/lab/rf_lab_computer_board.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 0, 0, 0 } },
    { RASTERFALL_PROP_ASSET_LAB_COMPUTER_COOLING, "lab_computer_cooling",
      "rasterfall/assets/models/props/lab/rf_lab_computer_cooling.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 0, 0, 0 } },
    { RASTERFALL_PROP_ASSET_LAB_COMPUTER_DISPLAY, "lab_computer_display",
      "rasterfall/assets/models/props/lab/rf_lab_computer_display.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 0, 0, 0 } },
    { RASTERFALL_PROP_ASSET_LAB_COMPUTER_KEYBOARD, "lab_computer_keyboard",
      "rasterfall/assets/models/props/lab/rf_lab_computer_keyboard.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 0, 0, 0 } },
    { RASTERFALL_PROP_ASSET_LAB_COMPUTER_CPU, "lab_computer_cpu",
      "rasterfall/assets/models/props/lab/rf_lab_computer_cpu.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 0, 0, 0 } },
    { RASTERFALL_PROP_ASSET_LAB_COMPUTER_MEMORY, "lab_computer_memory",
      "rasterfall/assets/models/props/lab/rf_lab_computer_memory.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 0, 0, 0 } },
    { RASTERFALL_PROP_ASSET_LAB_COMPUTER_COMPUTE, "lab_computer_compute",
      "rasterfall/assets/models/props/lab/rf_lab_computer_compute.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 0, 0, 0 } },
};

static const struct rasterfall_prop_asset_profile *find_id(int id)
{
    int i;
    for (i = 0; i < RASTERFALL_PROP_ASSET_COUNT; i++)
        if (prop_assets[i].id == id) return &prop_assets[i];
    return 0;
}

const struct rasterfall_prop_asset_profile *
rasterfall_prop_asset_profile(int id)
{
    return find_id(id);
}

const struct rasterfall_prop_asset_profile *
rasterfall_prop_asset_by_name(const char *name)
{
    int i;
    if (!name) return 0;
    for (i = 0; i < RASTERFALL_PROP_ASSET_COUNT; i++)
        if (!strcmp(prop_assets[i].name, name)) return &prop_assets[i];
    return 0;
}

int rasterfall_prop_render_scale(
    const struct rasterfall_prop_asset_profile *profile, int instance_scale_milli)
{
    if (!profile || instance_scale_milli <= 0) return 0;
    return (int)((long long)profile->render_scale_milli *
                 instance_scale_milli / 1000);
}

int rasterfall_prop_collision_dimensions(
    const struct rasterfall_prop_asset_profile *profile,
    int yaw_degrees, int instance_scale_milli,
    struct rasterfall_prop_dimensions *out)
{
    double angle, sine, cosine;
    int yaw, width, depth;
    if (!profile || !out || instance_scale_milli <= 0) return -1;
    width = (int)((long long)profile->collision_size.x *
                  instance_scale_milli / 1000);
    out->y = (int)((long long)profile->collision_size.y *
                   instance_scale_milli / 1000);
    depth = (int)((long long)profile->collision_size.z *
                  instance_scale_milli / 1000);
    if (width <= 0 || out->y <= 0 || depth <= 0) return -1;
    yaw = yaw_degrees % 360;
    if (yaw < 0) yaw += 360;
    angle = (double)yaw * 3.141592653589793 / 180.0;
    sine = sin(angle); if (sine < 0) sine = -sine;
    cosine = cos(angle); if (cosine < 0) cosine = -cosine;
    out->x = (int)(width * cosine + depth * sine + 0.999999);
    out->z = (int)(width * sine + depth * cosine + 0.999999);
    return out->x > 0 && out->z > 0 ? 0 : -1;
}

static int host_cpu_slots = -1, host_memory_slots = -1;
static int host_capture_time = -1;
static struct toy_platform_host_sample host_sample;
static int host_initialized;
static int host_live_logged;
static unsigned host_sample_time;
void rasterfall_host_set_capture_time(int time_ms) { host_capture_time=time_ms; }

void rasterfall_host_update(unsigned time_ms)
{
    if (host_capture_time>=0 && host_initialized) {
        host_sample.memory_used_mib=host_sample.memory_total_mib*9/16;
        for (int core=0;core<host_sample.physical_cores &&
                core<TOY_PLATFORM_HOST_CORES;++core) {
            host_sample.core_percent[core]=(unsigned char)((34+core*17)%100);
            host_sample.core_valid[core]=1;
        }
        return;
    }
    if (!host_initialized || (time_ms && time_ms-host_sample_time >= 1000u)) {
        toy_platform_host_sample(&host_sample);
        host_sample_time=time_ms;
        host_initialized=1;
        if (host_capture_time>=0) {
            rasterfall_host_update(time_ms);
            return;
        }
        if (!host_live_logged && host_sample.core_valid[0]) {
            host_live_logged=1;
            __printf("HOST-RACK live core01=%u%% memory=%llu/%llu MiB\n",
                (unsigned)host_sample.core_percent[0],host_sample.memory_used_mib,
                host_sample.memory_total_mib);
        }
    }
}

static int host_memory_slots_needed(void)
{
    unsigned long long total=host_sample.memory_total_mib;
    if (!host_sample.memory_valid) return 0;
    total=(total+RASTERFALL_HOST_MEMORY_SLOT_MIB-1)/
        RASTERFALL_HOST_MEMORY_SLOT_MIB;
    return total>24 ? 24 : (int)total;
}

static void host_memory_slot(unsigned long long total,unsigned long long used,
    int index,unsigned long long *capacity,unsigned long long *occupied,int *lit)
{
    unsigned long long offset=(unsigned long long)index*RASTERFALL_HOST_MEMORY_SLOT_MIB;
    *capacity=offset<total ? total-offset : 0;
    if (*capacity>RASTERFALL_HOST_MEMORY_SLOT_MIB)
        *capacity=RASTERFALL_HOST_MEMORY_SLOT_MIB;
    *occupied=used>offset ? used-offset : 0;
    if (*occupied>*capacity) *occupied=*capacity;
    *lit=*capacity ? (int)((*occupied*8+*capacity-1)/ *capacity) : 0;
}

static int host_slots_for_asset(int asset)
{
    int cpu=asset==RASTERFALL_PROP_ASSET_HOST_CPU_MODULE ||
        asset==RASTERFALL_PROP_ASSET_HOST_CPU_HEADER;
    int count=cpu ? host_sample.physical_cores : host_memory_slots_needed();
    if (cpu && host_cpu_slots>=0) count=host_cpu_slots;
    if (!cpu && host_memory_slots>=0) count=host_memory_slots;
    return count>24 ? 24 : count;
}

int rasterfall_host_object_active(const char *id)
{
    const char *p;
    int cpu, rack;
    if (!id || strncmp(id,"host_",5)) return 1;
    p=id+5;
    cpu=!strncmp(p,"cpu_r",5);
    if (!cpu && strncmp(p,"memory_r",8)) return 1;
    p+=cpu ? 5 : 8;
    if (*p<'1' || *p>'4' || p[1]!='_') return 1;
    rack=*p-'1';
    rasterfall_host_update(0);
    return host_slots_for_asset(cpu ? RASTERFALL_PROP_ASSET_HOST_CPU_MODULE :
        RASTERFALL_PROP_ASSET_HOST_MEMORY_MODULE)>rack*6;
}

int rasterfall_host_bay_asset(int asset, int bay, int active_slots)
{
    if (bay < 1 || bay > RASTERFALL_HOST_SLOT_COUNT ||
        (asset != RASTERFALL_PROP_ASSET_HOST_CPU_MODULE &&
         asset != RASTERFALL_PROP_ASSET_HOST_MEMORY_MODULE)) return asset;
    return bay <= active_slots ? asset : RASTERFALL_PROP_ASSET_HOST_BLANK_PANEL;
}

void rasterfall_host_set_active_slots(int cpu_slots, int memory_slots)
{
    host_cpu_slots = cpu_slots < 0 ? -1 : cpu_slots > 24 ? 24 : cpu_slots;
    host_memory_slots = memory_slots < 0 ? -1 : memory_slots > 24 ? 24 : memory_slots;
}

int rasterfall_prop_presented_asset(int asset, int bay)
{
    static int detected;
    int cpu, rack, slot, slots;
    if (asset<RASTERFALL_PROP_ASSET_HOST_RACK_FRAME ||
        asset>RASTERFALL_PROP_ASSET_HOST_MEMORY_HEADER) return asset;
    rasterfall_host_update(0);
    if (!detected) {
        detected = 1;
        __printf("HOST-RACK physical_cores=%d memory_mib=%llu cpu_racks=%d memory_racks=%d\n",
            host_sample.physical_cores,host_sample.memory_total_mib,
            (host_sample.physical_cores+5)/6,(host_memory_slots_needed()+5)/6);
    }
    if (bay<100) {
        if (asset==RASTERFALL_PROP_ASSET_HOST_CPU_MODULE ||
            asset==RASTERFALL_PROP_ASSET_HOST_MEMORY_MODULE)
            return rasterfall_host_bay_asset(asset,bay,host_slots_for_asset(asset));
        return asset;
    }
    /* Shared frame/fan/cables carry their family in the encoded hundreds:
     * CPU 100..139, Memory 200..239. */
    cpu=bay>=100 && bay<200;
    rack=(bay%100)/10;
    slot=bay%10;
    slots=host_slots_for_asset(cpu ? RASTERFALL_PROP_ASSET_HOST_CPU_MODULE :
        RASTERFALL_PROP_ASSET_HOST_MEMORY_MODULE);
    if (rack<0 || rack>=4 || rack*6>=slots) return 0;
    if (asset==RASTERFALL_PROP_ASSET_HOST_CPU_MODULE ||
        asset==RASTERFALL_PROP_ASSET_HOST_MEMORY_MODULE)
        return rasterfall_host_bay_asset(asset,slot,slots-rack*6);
    return asset;
}

/* Small presentation geometry in local RFU, +Z front, with explicit time.
 * No hardware polling, gameplay mutation, or static mesh invalidation here. */
static int host_quad(const struct rasterfall_prop_instance *p,
    rasterfall_host_quad_fn emit, void *context, double local[4][3], unsigned color)
{
    int points[4][3];
    double a=p->yaw_degrees*3.141592653589793/180.0;
    double sn=sin(a),cs=cos(a),scale=p->scale_milli/1000.0;
    for (int i=0;i<4;++i) {
        points[i][0]=p->x+(int)((local[i][0]*cs+local[i][2]*sn)*scale);
        points[i][1]=p->y+(int)(local[i][1]*scale);
        points[i][2]=p->z+(int)((local[i][2]*cs-local[i][0]*sn)*scale);
    }
    return emit(context,points,color);
}

static int host_front(const struct rasterfall_prop_instance *p,
    rasterfall_host_quad_fn emit,void *context,double x,double y,double w,double h,unsigned color)
{
    double q[4][3]={{x,y,258},{x+w,y,258},{x+w,y+h,258},{x,y+h,258}};
    return host_quad(p,emit,context,q,color);
}

static int host_front_text(const struct rasterfall_prop_instance *p,
    rasterfall_host_quad_fn emit,void *context,double x,double y,double w,double h,unsigned color)
{
    /* The model face reaches about +246 RFU. Keep the text in front of the
     * face and its backing, with a small depth gap from the progress strip. */
    double q[4][3]={{x,y,264},{x+w,y,264},{x+w,y+h,264},{x,y+h,264}};
    return host_quad(p,emit,context,q,color);
}

static unsigned host_glyph_row(char c,int row)
{
    /* Five columns, seven rows. Slashed zero, flagged one and open five
     * remain distinct after low-resolution world rendering. */
    static const unsigned char digits[10][7]={
        {14,19,21,21,21,25,14},{4,12,4,4,4,4,14},
        {14,17,1,2,4,8,31},{30,1,1,14,1,1,30},
        {2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},
        {14,17,17,14,17,17,14},{14,17,17,15,1,1,14}};
    if (c>='0' && c<='9') return digits[c-'0'][row];
    switch(c) {
    case 'C': { static const unsigned char v[7]={15,16,16,16,16,16,15};return v[row]; }
    case 'M': { static const unsigned char v[7]={17,27,21,21,17,17,17};return v[row]; }
    case '/': { static const unsigned char v[7]={1,1,2,4,8,16,16};return v[row]; }
    case '%': { static const unsigned char v[7]={17,2,2,4,8,8,17};return v[row]; }
    case '-': return row==3 ? 14 : 0;
    case '+': return row==3 ? 14 : row==2 || row==4 ? 4 : 0;
    default: return 0;
    }
}

static int host_text(const struct rasterfall_prop_instance *p,
    rasterfall_host_quad_fn emit,void *context,const char *label,
    int screen_x,int right_align,unsigned color)
{
    int length=(int)strlen(label);
    int start=screen_x-(right_align ? length*18-3 : 0);
    for(int i=0;i<length;++i)
        for(int row=0;row<7;++row) {
            unsigned bits=host_glyph_row(label[i],row);
            for(int col=0;col<5;++col)
                if (bits & (16u>>col))
                    /* On a +Z front face, screen-right is local -X.
                     * Mirror the placement, not the glyph bitmap. */
                    if (host_front_text(p,emit,context,
                            -(start+i*18+col*3+3),
                            72-row*4,3,4,color)<0) return -1;
        }
    return 0;
}

static unsigned host_cpu_load_color(int percent)
{
    if (percent>=95) return 0xff5c5c;
    if (percent>=80) return 0xf2c14e;
    return 0x36a8ff;
}

int rasterfall_host_activity(const struct rasterfall_prop_instance *p,
    unsigned time_ms,rasterfall_host_quad_fn emit,void *context)
{
    int cpu=p->asset_id==RASTERFALL_PROP_ASSET_HOST_CPU_MODULE;
    unsigned seed,t,color;
    int rack=p->length>=100 ? (p->length%100)/10 : 0;
    int bay=p->length>=100 ? p->length%10 : p->length;
    int number=rack*6+bay;
    unsigned long long used=0,capacity=0;
    int percent=0,lit=0;
    char slot_label[24],value[24];
    if (!cpu && p->asset_id!=RASTERFALL_PROP_ASSET_HOST_MEMORY_MODULE) return 0;
    if (host_capture_time>=0) time_ms=(unsigned)host_capture_time;
    seed=(unsigned)p->length*173u+(unsigned)p->x*7u+(unsigned)p->z*11u;
    t=time_ms+seed;
    color=cpu ? 0x36a8ff : 0x4be38a;
    /* Power is steady. Each activity channel has a distinct 100..400 ms burst. */
    if (host_front(p,emit,context,104,92,8,8,0x9dffc1)<0) return -1;
    for (int k=0;k<3;++k) {
        unsigned period=530u+(seed+(unsigned)k*137u)%470u;
        unsigned on=100u+(seed+(unsigned)k*71u)%301u;
        if (host_front(p,emit,context,120+k*13,92,7,7,
                (t+(unsigned)k*193u)%period<on ? color : 0x233039)<0) return -1;
    }
    if (cpu) {
        int core=number-1;
        if (core>=0 && core<TOY_PLATFORM_HOST_CORES && host_sample.core_valid[core])
            percent=host_sample.core_percent[core];
        snprintf(slot_label,sizeof(slot_label),"C%02d%s",number,
            number==24 && host_sample.physical_cores>24 ? "+" : "");
        snprintf(value,sizeof(value),"--%%");
        if (core>=0 && core<TOY_PLATFORM_HOST_CORES && host_sample.core_valid[core])
            snprintf(value,sizeof(value),"%d%%",percent);
        if (host_front(p,emit,context,-150,17,300,10,0x1e6faf)<0) return -1;
        if (percent && host_front(p,emit,context,-150,17,percent*3,10,
                host_cpu_load_color(percent))<0)
            return -1;
    } else {
        if (host_sample.memory_valid) {
            host_memory_slot(host_sample.memory_total_mib,host_sample.memory_used_mib,
                number-1,&capacity,&used,&lit);
            snprintf(value,sizeof(value),"%llu/%lluM",used,capacity);
        } else snprintf(value,sizeof(value),"--M");
        snprintf(slot_label,sizeof(slot_label),"M%02d%s",number,
            number==24 && host_sample.memory_total_mib>24*4096ULL ? "+" : "");
        for (int k=0;k<8;++k)
            if (host_front(p,emit,context,-150+k*38,17,30,10,
                    k<lit ? color : 0x1e8e59)<0) return -1;
    }
    if (host_front(p,emit,context,-164,43,328,40,0x0d151b)<0) return -1;
    if (host_text(p,emit,context,slot_label,-150,0,color)<0) return -1;
    if (host_text(p,emit,context,value,150,1,
            cpu && percent>=80 ? host_cpu_load_color(percent) : 0xdce8f2)<0)
        return -1;
    for (int side=-1;side<=1;side+=2) {
        if (cpu) {
            for (int fan=0;fan<2;++fan) {
                double center=fan ? 144 : -144;
                double a=(double)(t%10000)*.019+fan;
                for (int blade=0;blade<4;++blade) {
                    double b=a+blade*1.570796326794897;
                    double c=cos(b),s=sin(b);
                    double q[4][3]={{side*158,70+5*c,center+5*s},
                        {side*158,70+28*c,center+28*s},
                        {side*158,70+25*c-8*s,center+25*s+8*c},
                        {side*158,70+5*c-4*s,center+5*s+4*c}};
                    if (host_quad(p,emit,context,q,0x849196)<0) return -1;
                }
            }
        } else {
            for (int bank=0;bank<4;++bank) {
                double z=-130+bank*78;
                double q[4][3]={{side*151,65,z},{side*151,70,z},
                    {side*151,70,z+14},{side*151,65,z+14}};
                if (host_quad(p,emit,context,q,
                    (t+bank*217u)%910u<180u ? 0x427856 : 0x1c2921)<0) return -1;
            }
        }
    }
    return 0;
}

#include "render/rf_electronics_geometry.inc"

struct host_test_capture { unsigned hash,count; };
struct host_text_orientation_capture { int right_x,left_x,found_right,found_left; };
static int host_test_text_orientation(void *context,const int points[4][3],unsigned color)
{
    struct host_text_orientation_capture *c=context;
    (void)color;
    /* Glyph '2' has a lone right pixel in row 2 and a lone left in row 5. */
    if (points[0][1]==64) { c->right_x=points[0][0]; c->found_right++; }
    if (points[0][1]==52) { c->left_x=points[0][0]; c->found_left++; }
    return 0;
}
static int host_test_quad(void *context,const int points[4][3],unsigned color)
{
    struct host_test_capture *c=context;
    c->hash=(c->hash^color)*16777619u;
    for (int i=0;i<4;++i) for (int k=0;k<3;++k)
        c->hash=(c->hash^(unsigned)points[i][k])*16777619u;
    c->count++;
    return 0;
}

int rasterfall_prop_asset_logic_test(void)
{
    int i;
    unsigned long long capacity,occupied;
    int lit;
    const struct rasterfall_prop_asset_profile *crate;
    const struct rasterfall_prop_asset_profile *barrier;
    const struct rasterfall_prop_asset_profile *lamp;
    for (int yaw=0;yaw<=180;yaw+=180) {
        struct rasterfall_prop_instance p={RASTERFALL_PROP_ASSET_HOST_CPU_MODULE,
            0,0,0,yaw,1000,1};
        struct host_text_orientation_capture glyph={0};
        if (host_text(&p,host_test_text_orientation,&glyph,"2",-150,0,0)<0 ||
            glyph.found_right!=1 || glyph.found_left!=1 ||
            (yaw==0 ? glyph.right_x>=glyph.left_x :
                glyph.right_x<=glyph.left_x)) return 29;
    }
    if (host_cpu_load_color(79)!=0x36a8ff ||
        host_cpu_load_color(80)!=0xf2c14e ||
        host_cpu_load_color(94)!=0xf2c14e ||
        host_cpu_load_color(95)!=0xff5c5c) return 30;
    host_memory_slot(24*4096ULL,6096,0,&capacity,&occupied,&lit);
    if (capacity!=4096 || occupied!=4096 || lit!=8) return 24;
    host_memory_slot(24*4096ULL,6096,1,&capacity,&occupied,&lit);
    if (capacity!=4096 || occupied!=2000 || lit!=4) return 25;
    host_memory_slot(18*1024ULL,1,0,&capacity,&occupied,&lit);
    if (capacity!=4096 || occupied!=1 || lit!=1) return 26;
    host_memory_slot(18*1024ULL,0,4,&capacity,&occupied,&lit);
    if (capacity!=2048 || occupied!=0 || lit!=0) return 27;
    host_memory_slot(18*1024ULL,18*1024ULL,5,&capacity,&occupied,&lit);
    if (capacity || occupied || lit) return 28;
    for (int kind=0;kind<2;++kind) {
        struct rasterfall_prop_instance p={kind ? RASTERFALL_PROP_ASSET_HOST_MEMORY_MODULE :
            RASTERFALL_PROP_ASSET_HOST_CPU_MODULE,2700,0,3400,198,1000,1};
        struct host_test_capture a={0},b={0},c={0},d={0};
        rasterfall_host_activity(&p,0,host_test_quad,&a);
        rasterfall_host_activity(&p,0,host_test_quad,&b);
        rasterfall_host_activity(&p,300,host_test_quad,&c);
        p.length=2;
        rasterfall_host_activity(&p,0,host_test_quad,&d);
        if (!a.count || a.count!=b.count || a.hash!=b.hash ||
            a.hash==c.hash || a.hash==d.hash) return 22;
        p.asset_id=RASTERFALL_PROP_ASSET_HOST_BLANK_PANEL;
        d.count=0;
        rasterfall_host_activity(&p,300,host_test_quad,&d);
        if (d.count) return 23;
    }
    /* Every occupancy 0..6 is bottom-filled for both resource families. */
    for (int count = 0; count <= 6; ++count)
        for (i = 1; i <= 6; ++i) {
            int cpu = RASTERFALL_PROP_ASSET_HOST_CPU_MODULE;
            int mem = RASTERFALL_PROP_ASSET_HOST_MEMORY_MODULE;
            int blank = RASTERFALL_PROP_ASSET_HOST_BLANK_PANEL;
            if (rasterfall_host_bay_asset(cpu,i,count) != (i<=count ? cpu : blank) ||
                rasterfall_host_bay_asset(mem,i,count) != (i<=count ? mem : blank)) return 20;
        }
    if (rasterfall_host_bay_asset(RASTERFALL_PROP_ASSET_HOST_CPU_MODULE,0,0) !=
        RASTERFALL_PROP_ASSET_HOST_CPU_MODULE) return 21;
    rasterfall_host_set_active_slots(7,13);
    if (rasterfall_prop_presented_asset(RASTERFALL_PROP_ASSET_HOST_CPU_MODULE,111)!=
            RASTERFALL_PROP_ASSET_HOST_CPU_MODULE ||
        rasterfall_prop_presented_asset(RASTERFALL_PROP_ASSET_HOST_CPU_MODULE,112)!=
            RASTERFALL_PROP_ASSET_HOST_BLANK_PANEL ||
        rasterfall_prop_presented_asset(RASTERFALL_PROP_ASSET_HOST_RACK_FRAME,120)!=0 ||
        rasterfall_prop_presented_asset(RASTERFALL_PROP_ASSET_HOST_MEMORY_MODULE,221)!=
            RASTERFALL_PROP_ASSET_HOST_MEMORY_MODULE ||
        rasterfall_prop_presented_asset(RASTERFALL_PROP_ASSET_HOST_MEMORY_MODULE,222)!=
            RASTERFALL_PROP_ASSET_HOST_BLANK_PANEL ||
        rasterfall_prop_presented_asset(RASTERFALL_PROP_ASSET_HOST_RACK_FRAME,230)!=0)
        return 29;
    rasterfall_host_set_active_slots(-1,-1);
    for (i = 0; i < RASTERFALL_PROP_ASSET_COUNT; i++) {
        const struct rasterfall_prop_asset_profile *asset = prop_assets + i;
        int visual_only=(asset->id>=RASTERFALL_PROP_ASSET_CAMPUS_WALL_PLAIN &&
                         asset->id<=RASTERFALL_PROP_ASSET_BOUNDARY_WALL) ||
                        (asset->id>=RASTERFALL_PROP_ASSET_LAB_COMPUTER_CASE &&
                         asset->id<=RASTERFALL_PROP_ASSET_LAB_COMPUTER_COMPUTE);
        if (asset->id != i + 1 || !asset->name || !asset->model_path ||
            asset->render_scale_milli != RASTERFALL_PROP_RENDER_SCALE_MILLI ||
            (!visual_only &&
             (asset->collision_size.x <= 0 || asset->collision_size.y <= 0 ||
              asset->collision_size.z <= 0)) ||
            (visual_only &&
             (asset->collision_size.x || asset->collision_size.y || asset->collision_size.z)))
            return 1;
    }
    crate = rasterfall_prop_asset_profile(RASTERFALL_PROP_ASSET_CRATE);
    barrier = rasterfall_prop_asset_by_name("barrier");
    lamp = rasterfall_prop_asset_by_name("lamp_post");
    if (!crate || !barrier || !lamp || crate->id != RASTERFALL_PROP_ASSET_CRATE ||
        barrier->id != RASTERFALL_PROP_ASSET_BARRIER ||
        lamp->id != RASTERFALL_PROP_ASSET_LAMP_POST)
        return 2;
    if (barrier->collision_size.x != 1229 ||
        barrier->collision_size.y != 512 ||
        barrier->collision_size.z != 410 ||
        lamp->collision_size.x != 410 ||
        lamp->collision_size.y != 1638 ||
        lamp->collision_size.z != 410)
        return 7;
    if (rasterfall_prop_asset_profile(0) ||
        rasterfall_prop_asset_by_name("missing") ||
        rasterfall_prop_asset_by_name(0))
        return 3;
    if (crate->render_scale_milli != 2207 ||
        crate->collision_size.x != RASTERFALL_RFU_FROM_MM(1200) ||
        crate->collision_size.y != RASTERFALL_RFU_FROM_MM(1000) ||
        crate->collision_size.z != RASTERFALL_RFU_FROM_MM(1000))
        return 4;
    if (rasterfall_prop_render_scale(crate, 1000) != 2207 ||
        rasterfall_prop_render_scale(crate, 2000) != 4414 ||
        rasterfall_prop_render_scale(crate, 0) != 0)
        return 5;
    {
        struct rasterfall_prop_dimensions dimensions;
        for (i = RASTERFALL_PROP_ASSET_CAMPUS_WALL_PLAIN;
             i <= RASTERFALL_PROP_ASSET_BOUNDARY_WALL; i++) {
            if (rasterfall_prop_collision_dimensions(find_id(i), 0, 1000,
                                                     &dimensions) == 0)
                return 8;
        }
        if (rasterfall_prop_collision_dimensions(barrier, 0, 1000,
                                                  &dimensions) != 0 ||
            dimensions.x != 1229 || dimensions.y != 512 || dimensions.z != 410 ||
            rasterfall_prop_collision_dimensions(barrier, 90, 1000,
                                                  &dimensions) != 0 ||
            dimensions.x != 410 || dimensions.z != 1229)
            return 6;
    }
    return 0;
}
