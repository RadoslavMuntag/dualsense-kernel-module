/*
    Phase 2 : periodic "tick" with a configurable interval.

    No hardware is touched yet. Ticks simulate a blinking LED.
*/

#define pr_fmt(fmt)    \
	KBUILD_MODNAME \
	": " fmt // redefinition of pt_fmt to add prefix for pr_info()
// and pr_err() strings

#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/workqueue.h>

#define BLINK_MS_DEFAULT 1000
#define BLINK_MS_MIN 100
#define BLINK_MS_MAX 10000

static int blink_ms = BLINK_MS_DEFAULT;

/*
    Called upon load and writes to /sys/module/ds_demo/parameters/blink_ms.
*/
static int blink_ms_set(const char *val, const struct kernel_param *kp)
{
	int v, ret;

	ret = kstrtoint(val, 10, &v); // kernel string to int conversion
	if (ret)
		return ret; // not a number

	/* out of range */
	if (v < BLINK_MS_MIN || v > BLINK_MS_MAX) {
		pr_err("blink_ms=%d rejected, allowed range %d..%d\n", v,
		       BLINK_MS_MIN, BLINK_MS_MAX);
		/*
    program returns with -EINVAL = "invalid argument"
    */
		return -EINVAL;
	}

	/* Concurrency present, hence WRITE_ONCE used*/
	WRITE_ONCE(*(int *)kp->arg, v);
	pr_info("blink_ms set to %d\n", v);
	return 0;
}

static const struct kernel_param_ops blink_ms_ops = {
	.set = blink_ms_set,
	.get = param_get_int, /* standard reader for: cat .../blink_ms */
};

/*
    blink_ms - parameter name
    &blink_ms_ops - setter/getter table
    &blink_ms - the variable holding the current parrameter setting
    0644 - sysfs file permisions, wr - root, r - others
*/
module_param_cb(blink_ms, &blink_ms_ops, &blink_ms, 0644);
MODULE_PARM_DESC(blink_ms, "Tick interval in ms (100..10000, default 1000)");

DEFINE_LED_TRIGGER(ds_trig)

static void tick_fn(struct work_struct *work);
static DECLARE_DELAYED_WORK(tick_work, tick_fn);

static unsigned long tick_count;
static bool led_on;

static void tick_fn(struct work_struct *work)
{
	int ms = READ_ONCE(blink_ms);

	led_on = !led_on;
	tick_count++;

	led_trigger_event(ds_trig, led_on ? LED_FULL : LED_OFF);

	pr_info("tick %lu: LED would be %s (simulated, blink_ms=%d)\n",
		tick_count, led_on ? "ON" : "OFF", ms);

	schedule_delayed_work(&tick_work, msecs_to_jiffies(ms));
}

static int __init ds_demo_init(void)
{
	led_trigger_register_simple("ds-demo", &ds_trig);
	if (!ds_trig) {
		pr_err("failed to register LED trigger\n");
		return -ENOMEM;
	}

	pr_info("loaded, blink_ms=%d\n", blink_ms);
	schedule_delayed_work(&tick_work, msecs_to_jiffies(blink_ms));
	return 0;
}

static void __exit ds_demo_exit(void)
{
	/*
     Must finish before the module's code is freed. The _sync variant
     waits for a running tick_fn and also stops it from re-arming.
  */
	cancel_delayed_work_sync(&tick_work);
	led_trigger_event(ds_trig, LED_OFF);

	led_trigger_unregister_simple(ds_trig);

	pr_info("unloaded after %lu ticks\n", tick_count);
}

module_init(ds_demo_init);
module_exit(ds_demo_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Radoslav Muntag");
MODULE_DESCRIPTION("A sample driver for dualsense controller");
MODULE_VERSION("0.3");
