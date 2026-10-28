/* SPDX-License-Identifier: GPL-2.0-only */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/errno.h>

struct memory_state {
  int count;
  int limit;
};

static struct memory_state *state;

static int __init memory_lab_init(void)
{
  state = kzalloc(sizeof(*state), GFP_KERNEL);

  if (!state)
    return -ENOMEM;

  pr_info("memory_lab: initial count=%d limit=%d\n",
          state->count, state->limit);

  state->count = 1;
  state->limit = 100;

  pr_info("memory_lab: initialized\n");
  return 0;
}

static void __exit memory_lab_exit(void)
{
  pr_info("memory_lab: freeing memory\n");
  kfree(state);
  state = NULL;
}

module_init(memory_lab_init);
module_exit(memory_lab_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Kernel memory lifetime lab");
