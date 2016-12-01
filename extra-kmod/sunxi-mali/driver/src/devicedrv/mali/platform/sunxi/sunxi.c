
#include "mali_kernel_linux.h"
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/pm.h>
#ifdef CONFIG_PM_RUNTIME
#include <linux/pm_runtime.h>
#endif
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/version.h>

#include <linux/clk/clk-conf.h>

#include <linux/mali/mali_utgard.h>

#define MALI_PP_BASE(pp)	0x8000 + 0x2000 * (pp)
#define MALI_PP_VERSION_REG	0x1000

#define MALI_GP_VERSION_REG	0x6c
#define MALI_GP_VERSION_200		0x0a07
#define MALI_GP_VERSION_300		0x0c07
#define MALI_GP_VERSION_400		0x0b07
#define MALI_GP_VERSION_450		0x0d07

struct mali {
	struct clk		*bus_clk;
	struct clk		*mod_clk;

	struct reset_control	*reset;

	struct platform_device	*dev;
};

struct mali *mali = NULL;

struct resource *mali_create_mp1_resources(unsigned long address,
					   int irq_gp, int irq_gpmmu,
					   int irq_pp0, int irq_ppmmu0,
					   int *len)
{
	struct resource target[] = {
		MALI_GPU_RESOURCES_MALI400_MP1_PMU(address,
						   irq_gp, irq_gpmmu,
						   irq_pp0, irq_ppmmu0)
	};
	struct resource *res;

	res = kzalloc(sizeof(target), GFP_KERNEL);
	if (!res)
		return NULL;

	memcpy(res, target, sizeof(target));

	*len = ARRAY_SIZE(target);

	return res;
}

struct resource *mali_create_mp2_resources(unsigned long address,
					   int irq_gp, int irq_gpmmu,
					   int irq_pp0, int irq_ppmmu0,
					   int irq_pp1, int irq_ppmmu1,
					   int *len)
{
	struct resource target[] = {
		MALI_GPU_RESOURCES_MALI400_MP2_PMU(address,
						   irq_gp, irq_gpmmu,
						   irq_pp0, irq_ppmmu0,
						   irq_pp1, irq_ppmmu1)
	};
	struct resource *res;

	res = kzalloc(sizeof(target), GFP_KERNEL);
	if (!res)
		return NULL;

	memcpy(res, target, sizeof(target));

	*len = ARRAY_SIZE(target);

	return res;
}

static int mali_get_core_numbers(void __iomem *regs)
{
	int i;

	for (i = 0; i < 4; i++) {
		u32 val = readl(regs + MALI_PP_BASE(i) + MALI_PP_VERSION_REG);
		if (!val)
			break;
	}

	return i;
}	

static void mali_print_core_version(void __iomem *regs)
{
	unsigned int gpu_model, major, minor;
	u32 val;

	val = readl(regs + MALI_GP_VERSION_REG);
	minor = val & 0xff;
	major = (val >> 8) & 0xff;

	switch (val >> 16) {
	case MALI_GP_VERSION_400:
		gpu_model = 400;
		break;

	default:
		pr_err("Unrecognized GPU\n");
		return;
	}

	pr_info("Found ARM Mali %u MP%d (r%up%u)\n",
		gpu_model, mali_get_core_numbers(regs), major, minor);
};

static struct of_device_id mali_dt_ids[] = {
	{ .compatible = "arm,mali-400" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, mali_dt_ids);

int mali_platform_device_register(void)
{
	int irq_gp, irq_gpmmu;
	int irq_pp0, irq_ppmmu0;
	int irq_pp1 = -EINVAL, irq_ppmmu1 = -EINVAL;
	int irq_pmu;
	int nr_cores;
	struct resource *mali_res = NULL, res;
	struct device_node *np;
	void __iomem *regs;
	int ret, len;

	np = of_find_matching_node(NULL, mali_dt_ids);
	if (!np) {
		pr_err("Couldn't find the mali node\n");
		return -ENODEV;
	}

	mali = kzalloc(sizeof(*mali), GFP_KERNEL);
	if (!mali) {
		ret = -ENOMEM;
		goto err_put_node;
	}

	ret = of_clk_set_defaults(np, false);
	if (ret) {
		pr_err("Couldn't set clock defaults\n");
		goto err_free_mem;
	}

	mali->bus_clk = of_clk_get_by_name(np, "ahb");
	if (IS_ERR(mali->bus_clk)) {
		pr_err("Couldn't retrieve our bus clock\n");
		ret = PTR_ERR(mali->bus_clk);
		goto err_free_mem;
	}
	clk_prepare_enable(mali->bus_clk);

	mali->mod_clk = of_clk_get_by_name(np, "mod");
	if (IS_ERR(mali->mod_clk)) {
		pr_err("Couldn't retrieve our module clock\n");
		ret = PTR_ERR(mali->mod_clk);
		goto err_put_bus;
	}
	clk_prepare_enable(mali->mod_clk);

	mali->reset = of_reset_control_get(np, NULL);
	if (IS_ERR(mali->reset)) {
		pr_err("Couldn't retrieve our reset handle\n");
		ret = PTR_ERR(mali->reset);
		goto err_put_mod;
	}
	reset_control_deassert(mali->reset);

	ret = of_address_to_resource(np, 0, &res);
	if (ret) {
		pr_err("Couldn't retrieve our base address\n");
		goto err_put_reset;
	}

	regs = ioremap(res.start, resource_size(&res));
	if (!regs) {
		pr_err("Couldn't map our base address\n");
		goto err_put_reset;
	}

	nr_cores = mali_get_core_numbers(regs);
	if ((nr_cores < 1) || (nr_cores > 2)) {
		pr_err("Invalid number of GPU cores %d\n", nr_cores);
		goto err_put_reset;
	}

	printk("Number of cores %d\n", nr_cores);

	irq_gp = of_irq_get_byname(np, "IRQGP");
	if (irq_gp < 0) {
		pr_err("Couldn't retrieve our GP interrupt\n");
		ret = irq_gp;
		goto err_put_reset;
	}

	irq_gpmmu = of_irq_get_byname(np, "IRQGPMMU");
	if (irq_gpmmu < 0) {
		pr_err("Couldn't retrieve our GP MMU interrupt\n");
		ret = irq_gpmmu;
		goto err_put_reset;
	}

	irq_pp0 = of_irq_get_byname(np, "IRQPP0");
	if (irq_pp0 < 0) {
		pr_err("Couldn't retrieve our PP0 interrupt %d\n", irq_pp0);
		ret = irq_pp0;
		goto err_put_reset;
	}

	irq_ppmmu0 = of_irq_get_byname(np, "IRQPPMMU0");
	if (irq_ppmmu0 < 0) {
		pr_err("Couldn't retrieve our PP0 MMU interrupt\n");
		ret = irq_ppmmu0;
		goto err_put_reset;
	}

	if (nr_cores >= 2) {
		irq_pp1 = of_irq_get_byname(np, "IRQPP1");
		if (irq_pp1 < 0) {
			pr_err("Couldn't retrieve our PP1 interrupt\n");
			ret = irq_pp1;
			goto err_put_reset;
		}

		irq_ppmmu1 = of_irq_get_byname(np, "IRQPPMMU1");
		if (irq_ppmmu1 < 0) {
			pr_err("Couldn't retrieve our PP1 MMU interrupt\n");
			ret = irq_ppmmu1;
			goto err_put_reset;
		}
	}

	irq_pmu = of_irq_get_byname(np, "IRQPMU");
	if (irq_pmu < 0) {
		pr_err("Couldn't retrieve our PMU interrupt\n");
		ret = irq_pmu;
		goto err_put_reset;
	}

	mali->dev = platform_device_alloc("mali-utgard", 0);
	if (!mali->dev) {
		pr_err("Couldn't create platform device\n");
		ret = -EINVAL;
		goto err_put_reset;
	}
	mali->dev->dev.dma_mask = &mali->dev->dev.coherent_dma_mask;
	mali->dev->dev.coherent_dma_mask = DMA_BIT_MASK(32);

	if (nr_cores == 1)
		mali_res = mali_create_mp1_resources(res.start,
						     irq_gp, irq_gpmmu,
						     irq_pp0, irq_ppmmu0,
						     &len);
	else if (nr_cores == 2)
		mali_res = mali_create_mp2_resources(res.start,
						     irq_gp, irq_gpmmu,
						     irq_pp0, irq_ppmmu0,
						     irq_pp1, irq_ppmmu1,
						     &len);
	else
		mali_res = NULL;

	if (!mali_res) {
		pr_err("Couldn't create target resources\n");
		ret = -EINVAL;
		goto err_free_pdev;
	}

	ret = platform_device_add_resources(mali->dev, mali_res, len);
	if (ret) {
		pr_err("Couldn't add our resources\n");
		goto err_free_resources;
	}

	mali_print_core_version(regs);

	ret = platform_device_add(mali->dev);
	if (ret) {
		pr_err("Couldn't add our device\n");
		goto err_unprepare_clk;
	}

#ifdef CONFIG_PM_RUNTIME
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)
	pm_runtime_set_autosuspend_delay(&mali->dev->dev, 1000);
	pm_runtime_use_autosuspend(&mali->dev->dev);
#endif
	pm_runtime_enable(&mali->dev->dev);
#endif

	pr_info("Allwinner sunXi mali glue initialized\n");

	iounmap(regs);
	of_node_put(np);

	return 0;

err_unprepare_clk:
	clk_disable_unprepare(mali->mod_clk);
	clk_disable_unprepare(mali->bus_clk);
	reset_control_assert(mali->reset);
err_free_resources:
	kfree(mali_res);
err_free_pdev:
	platform_device_put(mali->dev);
err_put_reset:
	reset_control_put(mali->reset);
err_put_mod:
	clk_put(mali->mod_clk);
err_put_bus:
	clk_put(mali->bus_clk);
err_free_mem:
	kfree(mali);
err_put_node:
	of_node_put(np);
	return ret;
}

int mali_platform_device_unregister(void)
{
#ifdef CONFIG_PM_RUNTIME
	pm_runtime_disable(&mali->dev->dev);
#endif
	platform_device_del(mali->dev);
	platform_device_put(mali->dev);
	clk_disable_unprepare(mali->mod_clk);
	clk_disable_unprepare(mali->bus_clk);
	reset_control_assert(mali->reset);
	reset_control_put(mali->reset);
	clk_put(mali->mod_clk);
	clk_put(mali->bus_clk);
	kfree(mali);

	return 0;
}
