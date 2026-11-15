#include "power_fsm.h"

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define T_S       30u
#define T_MS      (T_S * 1000u)
#define DEEP_MS   (20u * 60u * 1000u)
#define GRACE_S   60u
#define GRACE_MS  (GRACE_S * 1000u)

static power_fsm_t s_fsm;
static power_fsm_input_t s_in;
static power_fsm_output_t s_out;

/* On battery, booted at t=0, last user activity at boot, everything else quiet. */
static void reset(uint32_t boot)
{
    power_fsm_init(&s_fsm);
    s_in = (power_fsm_input_t){
        .now_ms = boot,
        .boot_ms = boot,
        .last_user_ms = boot,
        .last_agent_ms = boot,
        .batt_present = true,
        .screen_timeout_s = T_S,
        .deep_timeout_ms = DEEP_MS,
        .usb_deep_timeout_ms = 0,
        .boot_grace_s = GRACE_S,
        .light_sleep_enabled = true,
        .brightness_pct = 75,
        .dim_pct = 10,
        .home_visible = true,
    };
}

static void at(uint32_t now)
{
    s_in.now_ms = now;
    power_fsm_update(&s_fsm, &s_in, &s_out);
}

static void expect_stage(power_stage_t stage)
{
    TEST_ASSERT_EQUAL_STRING(power_stage_name(stage), power_stage_name(s_out.stage));
}

static void test_battery_transitions(void)
{
    reset(0);
    at(0);
    expect_stage(POWER_STAGE_ACTIVE);
    TEST_ASSERT_EQUAL(75, s_out.backlight_pct);
    TEST_ASSERT_TRUE(s_out.hold_cpu_lock);
    TEST_ASSERT_FALSE(s_out.panel_sleep);
    TEST_ASSERT_EQUAL(POWER_WIFI_PS_MIN, s_out.wifi_ps);
    TEST_ASSERT_FALSE(s_out.allow_light_sleep);
    TEST_ASSERT_EQUAL(T_MS, s_out.dim_ms);
    TEST_ASSERT_EQUAL(2 * T_MS, s_out.off_ms);
    TEST_ASSERT_EQUAL(DEEP_MS, s_out.deep_ms);

    at(T_MS - 1);
    expect_stage(POWER_STAGE_ACTIVE);
    at(T_MS);
    expect_stage(POWER_STAGE_DIM);
    TEST_ASSERT_EQUAL(10, s_out.backlight_pct);
    TEST_ASSERT_TRUE(s_out.hold_cpu_lock);
    TEST_ASSERT_FALSE(s_out.panel_sleep);

    at(2 * T_MS - 1);
    expect_stage(POWER_STAGE_DIM);
    at(2 * T_MS);
    expect_stage(POWER_STAGE_OFF);
    TEST_ASSERT_EQUAL(0, s_out.backlight_pct);
    TEST_ASSERT_TRUE(s_out.panel_sleep);
    TEST_ASSERT_FALSE(s_out.hold_cpu_lock);
    TEST_ASSERT_EQUAL(POWER_WIFI_PS_MAX, s_out.wifi_ps);
    TEST_ASSERT_TRUE(s_out.allow_light_sleep);
    TEST_ASSERT_FALSE(s_out.enter_deep_sleep);
    TEST_ASSERT_EQUAL(POWER_BLOCK_IDLE, s_out.blockers);

    at(DEEP_MS - 1);
    TEST_ASSERT_FALSE(s_out.enter_deep_sleep);
    at(DEEP_MS);
    TEST_ASSERT_TRUE(s_out.enter_deep_sleep);
    TEST_ASSERT_EQUAL(POWER_DEEP_IDLE, s_out.deep_reason);
    TEST_ASSERT_EQUAL(0, s_out.blockers);
}

static void test_usb_transitions(void)
{
    reset(0);
    s_in.ext_power = true;
    at(0);
    TEST_ASSERT_EQUAL(2 * T_MS, s_out.dim_ms);
    TEST_ASSERT_EQUAL(10 * T_MS, s_out.off_ms);
    TEST_ASSERT_EQUAL(0, s_out.deep_ms);
    at(T_MS);
    expect_stage(POWER_STAGE_ACTIVE);
    at(2 * T_MS - 1);
    expect_stage(POWER_STAGE_ACTIVE);
    at(2 * T_MS);
    expect_stage(POWER_STAGE_DIM);
    at(10 * T_MS - 1);
    expect_stage(POWER_STAGE_DIM);
    at(10 * T_MS);
    expect_stage(POWER_STAGE_OFF);
    TEST_ASSERT_TRUE(s_out.panel_sleep);
    TEST_ASSERT_FALSE(s_out.hold_cpu_lock);
    /* External power: no light sleep and no MAX_MODEM even with the screen off. */
    TEST_ASSERT_EQUAL(POWER_WIFI_PS_MIN, s_out.wifi_ps);
    TEST_ASSERT_FALSE(s_out.allow_light_sleep);
    at(24u * 3600u * 1000u);
    expect_stage(POWER_STAGE_OFF);
    TEST_ASSERT_FALSE(s_out.enter_deep_sleep);
    TEST_ASSERT_EQUAL(POWER_BLOCK_NEVER, s_out.blockers);
}

static void test_usb_deep_timeout_when_configured(void)
{
    reset(0);
    s_in.ext_power = true;
    s_in.usb_deep_timeout_ms = 30u * 60u * 1000u;
    at(10 * T_MS);
    expect_stage(POWER_STAGE_OFF);
    at(30u * 60u * 1000u - 1);
    TEST_ASSERT_FALSE(s_out.enter_deep_sleep);
    at(30u * 60u * 1000u);
    TEST_ASSERT_TRUE(s_out.enter_deep_sleep);
}

static void test_user_activity_returns_to_active(void)
{
    reset(0);
    at(2 * T_MS);
    expect_stage(POWER_STAGE_OFF);
    s_in.last_user_ms = 2 * T_MS + 500;
    at(2 * T_MS + 500);
    expect_stage(POWER_STAGE_ACTIVE);
    TEST_ASSERT_EQUAL(75, s_out.backlight_pct);
    TEST_ASSERT_TRUE(s_out.hold_cpu_lock);
    at(3 * T_MS + 500);
    expect_stage(POWER_STAGE_DIM);
    s_in.last_user_ms = 3 * T_MS + 600;
    at(3 * T_MS + 600);
    expect_stage(POWER_STAGE_ACTIVE);
}

static void test_pending_caps_at_dim_and_blocks_deep(void)
{
    reset(0);
    s_in.pending_count = 1;
    at(T_MS);
    expect_stage(POWER_STAGE_DIM);
    at(2 * T_MS);
    expect_stage(POWER_STAGE_DIM);
    TEST_ASSERT_TRUE(s_out.hold_cpu_lock);
    at(DEEP_MS * 3);
    expect_stage(POWER_STAGE_DIM);
    TEST_ASSERT_FALSE(s_out.enter_deep_sleep);
    TEST_ASSERT_TRUE(s_out.blockers & POWER_BLOCK_PENDING);
    TEST_ASSERT_TRUE(s_out.blockers & POWER_BLOCK_STAGE);
    /* The queue empties: the off timeout ran out long ago. */
    s_in.pending_count = 0;
    at(DEEP_MS * 3 + 1);
    expect_stage(POWER_STAGE_OFF);
    TEST_ASSERT_TRUE(s_out.enter_deep_sleep);
}

static void test_pending_caps_forced_off(void)
{
    reset(0);
    at(1000);
    power_fsm_force_stage(&s_fsm, POWER_STAGE_OFF, s_in.last_user_ms);
    s_in.pending_count = 2;
    at(2000);
    expect_stage(POWER_STAGE_DIM);
}

/* Each guard alone keeps the device awake at a moment when deep sleep is otherwise due. */
static void deep_due_setup(void)
{
    reset(0);
    s_in.now_ms = DEEP_MS;
}

static void test_guard_usb_host(void)
{
    deep_due_setup();
    s_in.usb_host = true;
    at(DEEP_MS);
    TEST_ASSERT_FALSE(s_out.enter_deep_sleep);
    TEST_ASSERT_EQUAL(POWER_BLOCK_USB_HOST, s_out.blockers);
    TEST_ASSERT_FALSE(s_out.allow_light_sleep);
    s_in.usb_host = false;
    at(DEEP_MS + 1);
    TEST_ASSERT_TRUE(s_out.enter_deep_sleep);
}

static void test_guard_provisioning(void)
{
    deep_due_setup();
    s_in.provisioning = true;
    at(DEEP_MS);
    TEST_ASSERT_FALSE(s_out.enter_deep_sleep);
    TEST_ASSERT_EQUAL(POWER_BLOCK_PROVISIONING, s_out.blockers);
    TEST_ASSERT_FALSE(s_out.allow_light_sleep);
}

static void test_guard_boot_grace(void)
{
    reset(0);
    /* A short deep timeout makes the boot grace the only thing left in the way. */
    s_in.screen_timeout_s = 1;
    s_in.deep_timeout_ms = 5000;
    at(GRACE_MS - 1);
    expect_stage(POWER_STAGE_OFF);
    TEST_ASSERT_FALSE(s_out.enter_deep_sleep);
    TEST_ASSERT_EQUAL(POWER_BLOCK_BOOT_GRACE, s_out.blockers);
    TEST_ASSERT_FALSE(s_out.allow_light_sleep);
    at(GRACE_MS);
    TEST_ASSERT_TRUE(s_out.enter_deep_sleep);
    TEST_ASSERT_TRUE(s_out.allow_light_sleep);
}

static void test_guard_never(void)
{
    deep_due_setup();
    s_in.deep_timeout_ms = 0;
    at(DEEP_MS * 10);
    TEST_ASSERT_FALSE(s_out.enter_deep_sleep);
    TEST_ASSERT_EQUAL(POWER_BLOCK_NEVER, s_out.blockers);
}

static void test_guard_stage(void)
{
    deep_due_setup();
    at(DEEP_MS);
    power_fsm_force_stage(&s_fsm, POWER_STAGE_ACTIVE, s_in.last_user_ms);
    at(DEEP_MS + 1);
    expect_stage(POWER_STAGE_ACTIVE);
    TEST_ASSERT_FALSE(s_out.enter_deep_sleep);
    TEST_ASSERT_EQUAL(POWER_BLOCK_STAGE, s_out.blockers);
}

static void test_dry_run_ignores_usb_host_for_deep_only(void)
{
    deep_due_setup();
    s_in.usb_host = true;
    s_in.dry_run = true;
    at(DEEP_MS);
    TEST_ASSERT_TRUE(s_out.enter_deep_sleep);
    TEST_ASSERT_FALSE(s_out.allow_light_sleep);
}

static void test_light_sleep_setting(void)
{
    reset(0);
    s_in.light_sleep_enabled = false;
    at(2 * T_MS + GRACE_MS);
    expect_stage(POWER_STAGE_OFF);
    TEST_ASSERT_FALSE(s_out.allow_light_sleep);
    TEST_ASSERT_EQUAL(POWER_WIFI_PS_MAX, s_out.wifi_ps);
}

static void test_agent_traffic_postpones_deep_without_waking(void)
{
    reset(0);
    at(2 * T_MS);
    expect_stage(POWER_STAGE_OFF);
    s_in.last_agent_ms = DEEP_MS - 1000;
    at(DEEP_MS);
    expect_stage(POWER_STAGE_OFF);
    TEST_ASSERT_FALSE(s_out.enter_deep_sleep);
    TEST_ASSERT_EQUAL(POWER_BLOCK_IDLE, s_out.blockers);
    TEST_ASSERT_EQUAL(1000, s_out.agent_idle_ms);
    TEST_ASSERT_EQUAL(DEEP_MS, s_out.idle_ms);
    at(2 * DEEP_MS - 1001);
    TEST_ASSERT_FALSE(s_out.enter_deep_sleep);
    at(2 * DEEP_MS - 1000);
    TEST_ASSERT_TRUE(s_out.enter_deep_sleep);
    expect_stage(POWER_STAGE_OFF);
}

static void test_agent_traffic_does_not_brighten(void)
{
    reset(0);
    at(T_MS);
    expect_stage(POWER_STAGE_DIM);
    s_in.last_agent_ms = T_MS + 100;
    at(T_MS + 100);
    expect_stage(POWER_STAGE_DIM);
}

static void test_settings_shorter_timeout_mid_idle(void)
{
    reset(0);
    at(20000);
    expect_stage(POWER_STAGE_ACTIVE);
    s_in.screen_timeout_s = 15;
    at(20001);
    expect_stage(POWER_STAGE_DIM);
    at(30000);
    expect_stage(POWER_STAGE_OFF);
}

static void test_settings_longer_timeout_mid_idle(void)
{
    reset(0);
    at(2 * T_MS);
    expect_stage(POWER_STAGE_OFF);
    /* A longer timeout does not bring the screen back; only user activity does. */
    s_in.screen_timeout_s = 120;
    at(2 * T_MS + 1);
    expect_stage(POWER_STAGE_OFF);
    s_in.last_user_ms = 2 * T_MS + 2;
    at(2 * T_MS + 2);
    expect_stage(POWER_STAGE_ACTIVE);
    at(2 * T_MS + 2 + 119999);
    expect_stage(POWER_STAGE_ACTIVE);
    at(2 * T_MS + 2 + 120000);
    expect_stage(POWER_STAGE_DIM);
}

static void test_settings_deep_timeout_mid_idle(void)
{
    reset(0);
    at(DEEP_MS - 1000);
    TEST_ASSERT_FALSE(s_out.enter_deep_sleep);
    s_in.deep_timeout_ms = 10u * 60u * 1000u;
    at(DEEP_MS - 999);
    TEST_ASSERT_TRUE(s_out.enter_deep_sleep);
    s_in.deep_timeout_ms = 0;
    at(DEEP_MS - 998);
    TEST_ASSERT_FALSE(s_out.enter_deep_sleep);
}

static void test_screen_timeout_zero_keeps_active(void)
{
    reset(0);
    s_in.screen_timeout_s = 0;
    at(DEEP_MS * 2);
    expect_stage(POWER_STAGE_ACTIVE);
    TEST_ASSERT_FALSE(s_out.enter_deep_sleep);
}

static void test_ext_power_switch_uses_new_table(void)
{
    reset(0);
    at(T_MS);
    expect_stage(POWER_STAGE_DIM);
    /* The app reports a debounced ext_power change as user activity. */
    s_in.ext_power = true;
    s_in.last_user_ms = T_MS + 10;
    at(T_MS + 10);
    expect_stage(POWER_STAGE_ACTIVE);
    at(T_MS + 10 + 2 * T_MS);
    expect_stage(POWER_STAGE_DIM);
}

static void test_force_stage_until_activity(void)
{
    reset(0);
    at(1000);
    power_fsm_force_stage(&s_fsm, POWER_STAGE_OFF, s_in.last_user_ms);
    at(2000);
    expect_stage(POWER_STAGE_OFF);
    TEST_ASSERT_TRUE(s_out.panel_sleep);
    at(5000);
    expect_stage(POWER_STAGE_OFF);
    s_in.last_user_ms = 6000;
    at(6000);
    expect_stage(POWER_STAGE_ACTIVE);

    power_fsm_force_stage(&s_fsm, POWER_STAGE_DIM, s_in.last_user_ms);
    at(7000);
    expect_stage(POWER_STAGE_DIM);
    TEST_ASSERT_EQUAL(10, s_out.backlight_pct);
}

static void test_low_battery_ignored_on_ext_power_or_absent(void)
{
    reset(0);
    s_in.batt_critical = true;
    s_in.ext_power = true;
    at(GRACE_MS + 10000);
    TEST_ASSERT_FALSE(s_out.show_lowbatt);
    TEST_ASSERT_FALSE(s_out.enter_deep_sleep);
    s_in.ext_power = false;
    s_in.batt_present = false;
    at(GRACE_MS + 20000);
    TEST_ASSERT_FALSE(s_out.show_lowbatt);
    TEST_ASSERT_FALSE(s_out.enter_deep_sleep);
}

static void test_critical_battery_sleeps_after_card(void)
{
    reset(0);
    const uint32_t t0 = GRACE_MS + 1000;
    at(t0 - 1);
    s_in.batt_critical = true;
    /* A pending request does not keep a dying battery awake. */
    s_in.pending_count = 1;
    at(t0);
    TEST_ASSERT_TRUE(s_out.show_lowbatt);
    expect_stage(POWER_STAGE_ACTIVE);
    TEST_ASSERT_TRUE(s_out.backlight_pct > 0);
    TEST_ASSERT_FALSE(s_out.enter_deep_sleep);
    at(t0 + POWER_FSM_LOWBATT_MS - 1);
    TEST_ASSERT_FALSE(s_out.enter_deep_sleep);
    at(t0 + POWER_FSM_LOWBATT_MS);
    TEST_ASSERT_TRUE(s_out.enter_deep_sleep);
    TEST_ASSERT_EQUAL(POWER_DEEP_BATTERY, s_out.deep_reason);
}

static void test_critical_battery_guards(void)
{
    reset(0);
    s_in.batt_critical = true;
    /* Inside the boot grace: the card shows but the device stays up. */
    at(1000);
    TEST_ASSERT_TRUE(s_out.show_lowbatt);
    at(GRACE_MS - 1);
    TEST_ASSERT_FALSE(s_out.enter_deep_sleep);
    at(GRACE_MS);
    TEST_ASSERT_TRUE(s_out.enter_deep_sleep);

    reset(0);
    s_in.batt_critical = true;
    s_in.usb_host = true;
    at(GRACE_MS + 1000);
    at(GRACE_MS + 1000 + 60000);
    TEST_ASSERT_TRUE(s_out.show_lowbatt);
    TEST_ASSERT_FALSE(s_out.enter_deep_sleep);
    /* Dry-run reports the decision even with a host attached. */
    s_in.dry_run = true;
    at(GRACE_MS + 1000 + 60001);
    TEST_ASSERT_TRUE(s_out.enter_deep_sleep);
    TEST_ASSERT_EQUAL(POWER_DEEP_BATTERY, s_out.deep_reason);
}

static void test_critical_episode_restarts_card(void)
{
    reset(0);
    const uint32_t t0 = GRACE_MS;
    s_in.batt_critical = true;
    at(t0);
    at(t0 + 3000);
    s_in.batt_critical = false;
    at(t0 + 3500);
    TEST_ASSERT_FALSE(s_out.show_lowbatt);
    s_in.batt_critical = true;
    at(t0 + 4000);
    at(t0 + 4000 + POWER_FSM_LOWBATT_MS - 1);
    TEST_ASSERT_FALSE(s_out.enter_deep_sleep);
    at(t0 + 4000 + POWER_FSM_LOWBATT_MS);
    TEST_ASSERT_TRUE(s_out.enter_deep_sleep);
}

static void test_battery_wake_resleeps_inside_grace(void)
{
    reset(0);
    s_in.wake_reason_battery = true;
    s_in.batt_critical = true;
    at(0);
    TEST_ASSERT_TRUE(s_out.show_lowbatt);
    TEST_ASSERT_TRUE(s_out.wake_check_pending);
    expect_stage(POWER_STAGE_ACTIVE);
    at(POWER_FSM_WAKE_LOWBATT_MS - 1);
    TEST_ASSERT_FALSE(s_out.enter_deep_sleep);
    at(POWER_FSM_WAKE_LOWBATT_MS);
    TEST_ASSERT_TRUE(s_out.enter_deep_sleep);
    TEST_ASSERT_EQUAL(POWER_DEEP_BATTERY, s_out.deep_reason);
    TEST_ASSERT_TRUE(POWER_FSM_WAKE_LOWBATT_MS < GRACE_MS);
}

static void test_battery_wake_blocked_by_usb_host(void)
{
    reset(0);
    s_in.wake_reason_battery = true;
    s_in.batt_critical = true;
    at(0);
    TEST_ASSERT_TRUE(s_out.wake_check_pending);
    /* The host enumerates during the card. */
    s_in.usb_host = true;
    at(1500);
    TEST_ASSERT_FALSE(s_out.wake_check_pending);
    at(POWER_FSM_WAKE_LOWBATT_MS);
    TEST_ASSERT_FALSE(s_out.enter_deep_sleep);
    /* Once cancelled the decision stays made for this boot. */
    s_in.usb_host = false;
    at(POWER_FSM_WAKE_LOWBATT_MS + 1000);
    TEST_ASSERT_FALSE(s_out.wake_check_pending);
    TEST_ASSERT_FALSE(s_out.enter_deep_sleep);
}

static void test_battery_wake_cancelled_by_ext_power(void)
{
    reset(0);
    s_in.wake_reason_battery = true;
    s_in.batt_critical = true;
    s_in.ext_power = true;
    at(0);
    TEST_ASSERT_FALSE(s_out.wake_check_pending);
    TEST_ASSERT_FALSE(s_out.show_lowbatt);
    at(POWER_FSM_WAKE_LOWBATT_MS);
    TEST_ASSERT_FALSE(s_out.enter_deep_sleep);
}

static void test_battery_wake_recovered_voltage(void)
{
    reset(0);
    s_in.wake_reason_battery = true;
    s_in.batt_critical = false;
    at(0);
    TEST_ASSERT_FALSE(s_out.wake_check_pending);
    TEST_ASSERT_FALSE(s_out.show_lowbatt);
    expect_stage(POWER_STAGE_ACTIVE);
}

static void test_battery_wake_dry_run_with_host(void)
{
    reset(0);
    s_in.wake_reason_battery = true;
    s_in.batt_critical = true;
    s_in.usb_host = true;
    s_in.dry_run = true;
    at(0);
    TEST_ASSERT_TRUE(s_out.wake_check_pending);
    at(POWER_FSM_WAKE_LOWBATT_MS);
    TEST_ASSERT_TRUE(s_out.enter_deep_sleep);
}

static void test_wrap_around(void)
{
    const uint32_t boot = UINT32_MAX - 10000;
    reset(boot);
    at(boot + T_MS);
    expect_stage(POWER_STAGE_DIM);
    at(boot + 2 * T_MS);
    expect_stage(POWER_STAGE_OFF);
    TEST_ASSERT_EQUAL(POWER_BLOCK_IDLE, s_out.blockers);
    s_in.last_agent_ms = boot + 3 * T_MS;
    at(boot + DEEP_MS);
    TEST_ASSERT_FALSE(s_out.enter_deep_sleep);
    at(boot + 3 * T_MS + DEEP_MS);
    TEST_ASSERT_TRUE(s_out.enter_deep_sleep);
    s_in.last_user_ms = boot + 3 * T_MS + DEEP_MS + 1;
    at(boot + 3 * T_MS + DEEP_MS + 1);
    expect_stage(POWER_STAGE_ACTIVE);
    TEST_ASSERT_FALSE(s_out.enter_deep_sleep);
}

static void test_wrap_around_battery_timers(void)
{
    const uint32_t boot = UINT32_MAX - 70000;
    reset(boot);
    s_in.batt_critical = true;
    at(boot + GRACE_MS);
    TEST_ASSERT_TRUE(s_out.show_lowbatt);
    at(boot + GRACE_MS + POWER_FSM_LOWBATT_MS - 1);
    TEST_ASSERT_FALSE(s_out.enter_deep_sleep);
    at(boot + GRACE_MS + POWER_FSM_LOWBATT_MS);
    TEST_ASSERT_TRUE(s_out.enter_deep_sleep);
}

static void test_timestamp_ahead_of_now(void)
{
    reset(0);
    s_in.last_user_ms = 5000;
    at(4000);
    TEST_ASSERT_EQUAL(0, s_out.idle_ms);
    expect_stage(POWER_STAGE_ACTIVE);
}

/* Pseudo-random 1..60 s gaps, as from a link that keeps retrying an unreachable server. */
static uint32_t s_rng = 12345;

static uint32_t next_gap_ms(void)
{
    s_rng = s_rng * 1103515245u + 12345u;
    return 1000u + (s_rng >> 8) % 60000u;
}

static void test_reconnect_loop_on_battery_reaches_deep_sleep(void)
{
    reset(0);
    uint32_t now = 0;
    uint32_t dim_at = 0;
    uint32_t off_at = 0;
    uint32_t deep_at = 0;
    while (now < 3600u * 1000u && deep_at == 0) {
        /* The fsm also runs on the app's 1 Hz tick between link events. */
        const uint32_t next = now + next_gap_ms();
        for (; now < next; now += 1000) {
            at(now);
            if (s_out.stage == POWER_STAGE_DIM && dim_at == 0) {
                dim_at = now;
            }
            if (s_out.stage == POWER_STAGE_OFF && off_at == 0) {
                off_at = now;
            }
            if (s_out.enter_deep_sleep && deep_at == 0) {
                deep_at = now;
                break;
            }
        }
    }
    TEST_ASSERT_EQUAL(T_MS, dim_at);
    TEST_ASSERT_EQUAL(2 * T_MS, off_at);
    TEST_ASSERT_EQUAL(DEEP_MS, deep_at);
}

static void test_names(void)
{
    power_stage_t s;
    TEST_ASSERT_TRUE(power_stage_parse("dim", &s));
    TEST_ASSERT_EQUAL(POWER_STAGE_DIM, s);
    TEST_ASSERT_FALSE(power_stage_parse("deep", &s));
    TEST_ASSERT_EQUAL_STRING("usb_host", power_block_name(POWER_BLOCK_USB_HOST));
    TEST_ASSERT_EQUAL_STRING("boot_grace", power_block_name(POWER_BLOCK_BOOT_GRACE));
    TEST_ASSERT_EQUAL_STRING("", power_block_name(3));
    TEST_ASSERT_EQUAL_STRING("battery", power_deep_reason_name(POWER_DEEP_BATTERY));
    TEST_ASSERT_EQUAL_STRING("", power_stage_name(POWER_STAGE_COUNT));
}

static void test_card_visible_holds_active(void)
{
    reset(0);
    s_in.pending_count = 1;
    s_in.card_visible = true;
    at(10 * T_MS);
    expect_stage(POWER_STAGE_ACTIVE);
    TEST_ASSERT_EQUAL(75, s_out.backlight_pct);
    TEST_ASSERT_TRUE(s_out.blockers & POWER_BLOCK_STAGE);
    TEST_ASSERT_FALSE(s_out.enter_deep_sleep);
    /* A console-forced stage does not darken a card either. */
    power_fsm_force_stage(&s_fsm, POWER_STAGE_OFF, s_in.last_user_ms);
    at(10 * T_MS + 10);
    expect_stage(POWER_STAGE_ACTIVE);
}

static void test_card_close_restarts_timers(void)
{
    reset(0);
    at(T_MS / 2);
    s_in.pending_count = 1;
    s_in.card_visible = true;
    at(5 * T_MS);
    expect_stage(POWER_STAGE_ACTIVE);
    /* The card closes long after the last press: the timers count from the close. */
    s_in.pending_count = 0;
    s_in.card_visible = false;
    const uint32_t closed = 6 * T_MS;
    at(closed);
    expect_stage(POWER_STAGE_ACTIVE);
    TEST_ASSERT_EQUAL_UINT32(0, s_out.idle_ms);
    at(closed + T_MS - 1);
    expect_stage(POWER_STAGE_ACTIVE);
    at(closed + T_MS);
    expect_stage(POWER_STAGE_DIM);
    at(closed + 2 * T_MS);
    expect_stage(POWER_STAGE_OFF);
    /* A newer press still counts from the press. */
    s_in.last_user_ms = closed + 3 * T_MS;
    at(closed + 3 * T_MS + 10);
    expect_stage(POWER_STAGE_ACTIVE);
}

static void test_card_hidden_keeps_the_pending_cap(void)
{
    reset(0);
    s_in.pending_count = 1;
    s_in.card_visible = false;
    at(3 * T_MS);
    expect_stage(POWER_STAGE_DIM);
}


/* ux-flows 5, every cell: stage x (agent idle | busy) x power x day/night x Show clock. */
static void test_clock_table(void)
{
    typedef struct {
        const char *name;
        uint32_t idle_ms;
        bool ext;
        bool agent_idle;
        bool night;
        power_clock_mode_t mode;
        uint16_t timeout_s;
        power_stage_t stage;
        bool clock;
        uint8_t pct;
    } row_t;
    /* T = 30 s: battery dim at 30 s and off at 60 s; external power dim at 60 s and off at 300 s. */
    static const row_t rows[] = {
        {"active idle", 10000, false, true, false, POWER_CLOCK_RESTING, T_S, POWER_STAGE_ACTIVE, false, 75},
        {"active busy", 10000, false, false, false, POWER_CLOCK_RESTING, T_S, POWER_STAGE_ACTIVE, false, 75},
        {"never timeout, 1 min", 60000, false, true, false, POWER_CLOCK_RESTING, 0, POWER_STAGE_ACTIVE, false, 75},
        {"never timeout, 2 min", 120000, false, true, false, POWER_CLOCK_RESTING, 0, POWER_STAGE_ACTIVE, true, 10},
        {"never timeout, busy", 600000, false, false, false, POWER_CLOCK_RESTING, 0, POWER_STAGE_ACTIVE, false, 75},
        {"dim batt day", 31000, false, true, false, POWER_CLOCK_RESTING, T_S, POWER_STAGE_DIM, true, 10},
        {"dim batt night", 31000, false, true, true, POWER_CLOCK_RESTING, T_S, POWER_STAGE_DIM, true, 5},
        {"dim ext day", 61000, true, true, false, POWER_CLOCK_RESTING, T_S, POWER_STAGE_DIM, true, 30},
        {"dim ext night", 61000, true, true, true, POWER_CLOCK_RESTING, T_S, POWER_STAGE_DIM, true, 5},
        {"dim busy batt", 31000, false, false, false, POWER_CLOCK_RESTING, T_S, POWER_STAGE_DIM, false, 10},
        {"dim busy ext", 61000, true, false, true, POWER_CLOCK_RESTING, T_S, POWER_STAGE_DIM, false, 10},
        {"dim never mode", 31000, false, true, false, POWER_CLOCK_NEVER, T_S, POWER_STAGE_DIM, false, 10},
        {"off batt", 61000, false, true, false, POWER_CLOCK_RESTING, T_S, POWER_STAGE_OFF, false, 0},
        {"off ext resting", 301000, true, true, false, POWER_CLOCK_RESTING, T_S, POWER_STAGE_OFF, false, 0},
        {"off ext always", 301000, true, true, false, POWER_CLOCK_ALWAYS_USB, T_S, POWER_STAGE_DIM, true, 30},
        {"off ext always night", 301000, true, true, true, POWER_CLOCK_ALWAYS_USB, T_S, POWER_STAGE_DIM, true, 5},
        {"off ext always busy", 301000, true, false, false, POWER_CLOCK_ALWAYS_USB, T_S, POWER_STAGE_OFF, false, 0},
        {"off batt always", 61000, false, true, false, POWER_CLOCK_ALWAYS_USB, T_S, POWER_STAGE_OFF, false, 0},
        {"dim batt always", 31000, false, true, false, POWER_CLOCK_ALWAYS_USB, T_S, POWER_STAGE_DIM, true, 10},
    };
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        const row_t *r = &rows[i];
        reset(0);
        s_in.boot_grace_s = 0;
        s_in.deep_timeout_ms = 0;
        s_in.ext_power = r->ext;
        s_in.agent_idle = r->agent_idle;
        s_in.night = r->night;
        s_in.clock_mode = r->mode;
        s_in.clock_brightness_pct = 30;
        s_in.screen_timeout_s = r->timeout_s;
        at(r->idle_ms);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(power_stage_name(r->stage), power_stage_name(s_out.stage), r->name);
        TEST_ASSERT_EQUAL_MESSAGE(r->clock, s_out.show_clock, r->name);
        TEST_ASSERT_EQUAL_MESSAGE(r->pct, s_out.backlight_pct, r->name);
        TEST_ASSERT_EQUAL_MESSAGE(r->stage == POWER_STAGE_OFF, s_out.panel_sleep, r->name);
        TEST_ASSERT_EQUAL_MESSAGE(r->stage != POWER_STAGE_OFF && !r->clock, s_out.hold_cpu_lock, r->name);
    }
}

static void test_clock_needs_nothing_pending(void)
{
    reset(0);
    s_in.agent_idle = true;
    s_in.pending_count = 1;
    at(31000);
    expect_stage(POWER_STAGE_DIM);
    TEST_ASSERT_FALSE(s_out.show_clock);
    TEST_ASSERT_EQUAL(10, s_out.backlight_pct);
    s_in.pending_count = 0;
    s_in.card_visible = true;
    at(32000);
    expect_stage(POWER_STAGE_ACTIVE);
    TEST_ASSERT_FALSE(s_out.show_clock);
    s_in.card_visible = false;
    at(33000);
    TEST_ASSERT_FALSE(s_out.show_clock);
    /* The closed card counts as activity: the clock comes back one timeout later. */
    at(33000 + T_MS);
    TEST_ASSERT_TRUE(s_out.show_clock);
}

/* Settings or the showcase open as the screen dims: the backlight stays at dim and the CPU lock is held. */
static void test_clock_needs_home(void)
{
    reset(0);
    s_in.agent_idle = true;
    s_in.home_visible = false;
    at(31000);
    expect_stage(POWER_STAGE_DIM);
    TEST_ASSERT_FALSE(s_out.show_clock);
    TEST_ASSERT_EQUAL(10, s_out.backlight_pct);
    TEST_ASSERT_TRUE(s_out.hold_cpu_lock);
    /* Back on home in the same stage: the clock replaces it. */
    s_in.home_visible = true;
    at(32000);
    expect_stage(POWER_STAGE_DIM);
    TEST_ASSERT_TRUE(s_out.show_clock);
    TEST_ASSERT_FALSE(s_out.hold_cpu_lock);

    /* Always on USB power does not keep another screen lit past its timeout. */
    reset(0);
    s_in.boot_grace_s = 0;
    s_in.deep_timeout_ms = 0;
    s_in.agent_idle = true;
    s_in.ext_power = true;
    s_in.clock_mode = POWER_CLOCK_ALWAYS_USB;
    s_in.home_visible = false;
    at(301000);
    expect_stage(POWER_STAGE_OFF);
    TEST_ASSERT_FALSE(s_out.show_clock);

    /* Screen timeout Never: no clock over Settings after two minutes. */
    reset(0);
    s_in.agent_idle = true;
    s_in.screen_timeout_s = 0;
    s_in.home_visible = false;
    at(POWER_FSM_NEVER_CLOCK_MS + 1000);
    expect_stage(POWER_STAGE_ACTIVE);
    TEST_ASSERT_FALSE(s_out.show_clock);
    TEST_ASSERT_EQUAL(75, s_out.backlight_pct);
    TEST_ASSERT_TRUE(s_out.hold_cpu_lock);
}

static void test_clock_battery_window(void)
{
    /* Default timeouts on battery: the clock is visible from 30 s to 60 s of idle. */
    reset(0);
    s_in.agent_idle = true;
    for (uint32_t t = 0; t <= 70000; t += 1000) {
        at(t);
        const bool expect = t >= T_MS && t < 2 * T_MS;
        TEST_ASSERT_EQUAL(expect, s_out.show_clock);
    }
}

static void test_night_switch_changes_backlight(void)
{
    reset(0);
    s_in.agent_idle = true;
    s_in.ext_power = true;
    s_in.clock_brightness_pct = 30;
    at(61000);
    TEST_ASSERT_EQUAL(30, s_out.backlight_pct);
    s_in.night = true;
    at(62000);
    TEST_ASSERT_EQUAL(5, s_out.backlight_pct);
    s_in.night = false;
    at(63000);
    TEST_ASSERT_EQUAL(30, s_out.backlight_pct);
}

static void test_sleep_warning(void)
{
    reset(0);
    s_in.boot_grace_s = 0;
    at(DEEP_MS - POWER_FSM_SLEEP_WARN_MS - 1);
    TEST_ASSERT_FALSE(s_out.sleep_warning);
    expect_stage(POWER_STAGE_OFF);
    at(DEEP_MS - POWER_FSM_SLEEP_WARN_MS);
    TEST_ASSERT_TRUE(s_out.sleep_warning);
    TEST_ASSERT_EQUAL_UINT32(POWER_FSM_SLEEP_WARN_MS, s_out.sleep_in_ms);
    expect_stage(POWER_STAGE_DIM);
    TEST_ASSERT_EQUAL(75, s_out.backlight_pct);
    TEST_ASSERT_FALSE(s_out.panel_sleep);
    TEST_ASSERT_FALSE(s_out.enter_deep_sleep);
    at(DEEP_MS - 4000);
    TEST_ASSERT_EQUAL_UINT32(4000, s_out.sleep_in_ms);
    /* Any button is user activity: the warning ends and the screen is active again. */
    s_in.last_user_ms = DEEP_MS - 3000;
    at(DEEP_MS - 3000);
    TEST_ASSERT_FALSE(s_out.sleep_warning);
    expect_stage(POWER_STAGE_ACTIVE);
    TEST_ASSERT_FALSE(s_out.enter_deep_sleep);

    /* Left alone, the warning runs out into the deep sleep. */
    reset(0);
    s_in.boot_grace_s = 0;
    at(DEEP_MS - 5000);
    TEST_ASSERT_TRUE(s_out.sleep_warning);
    at(DEEP_MS);
    TEST_ASSERT_FALSE(s_out.sleep_warning);
    TEST_ASSERT_TRUE(s_out.enter_deep_sleep);

    /* No warning while something else blocks the sleep. */
    reset(0);
    s_in.boot_grace_s = 0;
    s_in.usb_host = true;
    at(DEEP_MS - 5000);
    TEST_ASSERT_FALSE(s_out.sleep_warning);
    s_in.usb_host = false;
    s_in.deep_timeout_ms = 0;
    at(DEEP_MS - 5000);
    TEST_ASSERT_FALSE(s_out.sleep_warning);
}

static void test_low_pct_toasts(void)
{
    reset(0);
    const struct {
        uint8_t pct;
        bool ext;
        uint8_t toast;
    } steps[] = {
        {40, false, 0}, {16, false, 0}, {15, false, 15}, {14, false, 0}, {9, false, 0}, {8, false, 8},
        {5, false, 0},  {5, true, 0},   {5, false, 8},   {50, true, 0},  {12, false, 15}, {3, false, 8},
    };
    for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
        s_in.batt_pct = steps[i].pct;
        s_in.ext_power = steps[i].ext;
        at((uint32_t)i * 1000u);
        TEST_ASSERT_EQUAL_MESSAGE(steps[i].toast, s_out.low_toast_pct, "step");
    }
    reset(0);
    s_in.batt_present = false;
    s_in.batt_pct = 3;
    at(1000);
    TEST_ASSERT_EQUAL(0, s_out.low_toast_pct);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_battery_transitions);
    RUN_TEST(test_usb_transitions);
    RUN_TEST(test_usb_deep_timeout_when_configured);
    RUN_TEST(test_user_activity_returns_to_active);
    RUN_TEST(test_pending_caps_at_dim_and_blocks_deep);
    RUN_TEST(test_pending_caps_forced_off);
    RUN_TEST(test_guard_usb_host);
    RUN_TEST(test_guard_provisioning);
    RUN_TEST(test_guard_boot_grace);
    RUN_TEST(test_guard_never);
    RUN_TEST(test_guard_stage);
    RUN_TEST(test_dry_run_ignores_usb_host_for_deep_only);
    RUN_TEST(test_light_sleep_setting);
    RUN_TEST(test_agent_traffic_postpones_deep_without_waking);
    RUN_TEST(test_agent_traffic_does_not_brighten);
    RUN_TEST(test_settings_shorter_timeout_mid_idle);
    RUN_TEST(test_settings_longer_timeout_mid_idle);
    RUN_TEST(test_settings_deep_timeout_mid_idle);
    RUN_TEST(test_screen_timeout_zero_keeps_active);
    RUN_TEST(test_ext_power_switch_uses_new_table);
    RUN_TEST(test_force_stage_until_activity);
    RUN_TEST(test_low_battery_ignored_on_ext_power_or_absent);
    RUN_TEST(test_critical_battery_sleeps_after_card);
    RUN_TEST(test_critical_battery_guards);
    RUN_TEST(test_critical_episode_restarts_card);
    RUN_TEST(test_battery_wake_resleeps_inside_grace);
    RUN_TEST(test_battery_wake_blocked_by_usb_host);
    RUN_TEST(test_battery_wake_cancelled_by_ext_power);
    RUN_TEST(test_battery_wake_recovered_voltage);
    RUN_TEST(test_battery_wake_dry_run_with_host);
    RUN_TEST(test_wrap_around);
    RUN_TEST(test_wrap_around_battery_timers);
    RUN_TEST(test_timestamp_ahead_of_now);
    RUN_TEST(test_reconnect_loop_on_battery_reaches_deep_sleep);
    RUN_TEST(test_names);
    RUN_TEST(test_card_visible_holds_active);
    RUN_TEST(test_card_close_restarts_timers);
    RUN_TEST(test_card_hidden_keeps_the_pending_cap);
    RUN_TEST(test_clock_table);
    RUN_TEST(test_clock_needs_nothing_pending);
    RUN_TEST(test_clock_needs_home);
    RUN_TEST(test_clock_battery_window);
    RUN_TEST(test_night_switch_changes_backlight);
    RUN_TEST(test_sleep_warning);
    RUN_TEST(test_low_pct_toasts);
    return UNITY_END();
}
