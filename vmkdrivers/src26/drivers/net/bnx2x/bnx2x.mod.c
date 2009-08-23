#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

MODULE_INFO(vermagic, VERMAGIC_STRING);

struct module __this_module
__attribute__((section(".gnu.linkonce.this_module"))) = {
 .name = KBUILD_MODNAME,
 .init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
 .exit = cleanup_module,
#endif
};

static const struct modversion_info ____versions[]
__attribute_used__
__attribute__((section("__versions"))) = {
	{ 0x8e4729ed, "struct_module" },
	{ 0x3ce4ca6f, "disable_irq" },
	{ 0x12da5bb2, "__kmalloc" },
	{ 0xf9a482f9, "msleep" },
	{ 0xba7921dc, "zlib_inflateEnd" },
	{ 0xe081c0e7, "mem_map" },
	{ 0xd6ee688f, "vmalloc" },
	{ 0x89b301d4, "param_get_int" },
	{ 0x2d55790e, "alloc_etherdev" },
	{ 0xe1b7029c, "print_tainted" },
	{ 0xab978df6, "malloc_sizes" },
	{ 0xa956ceb5, "pci_disable_device" },
	{ 0xc7a4fbed, "rtnl_lock" },
	{ 0x26a4ee81, "pci_disable_msix" },
	{ 0x66f7e907, "netif_carrier_on" },
	{ 0x1bcd461f, "_spin_lock" },
	{ 0x27dc2d10, "ethtool_op_get_sg" },
	{ 0xf6a5a6c8, "schedule_work" },
	{ 0x55f78f43, "netif_carrier_off" },
	{ 0xeae3dfd6, "__const_udelay" },
	{ 0xa4ee06c8, "pci_release_regions" },
	{ 0xdf2126f8, "mutex_unlock" },
	{ 0xcb6beb40, "hweight32" },
	{ 0x2fd1d81c, "vfree" },
	{ 0x2b3d8a8e, "pci_bus_write_config_word" },
	{ 0x98bd6f46, "param_set_int" },
	{ 0x7d11c268, "jiffies" },
	{ 0xda4008e6, "cond_resched" },
	{ 0xb3377fc1, "__netdev_alloc_skb" },
	{ 0x5f93e76c, "__pskb_pull_tail" },
	{ 0xedcebc37, "pci_set_master" },
	{ 0x55976d8f, "__alloc_pages" },
	{ 0xc659d5a, "del_timer_sync" },
	{ 0x6fcb87a1, "touch_softlockup_watchdog" },
	{ 0xccc26fc, "pci_set_dma_mask" },
	{ 0xfe1ea07e, "pci_enable_msix" },
	{ 0x44642aeb, "pci_restore_state" },
	{ 0x86cb9d9f, "__mutex_init" },
	{ 0x1b7d4074, "printk" },
	{ 0xbf1e4266, "ethtool_op_get_link" },
	{ 0x376da2ef, "free_netdev" },
	{ 0x6c6a21c9, "register_netdev" },
	{ 0xc871cb49, "dma_free_coherent" },
	{ 0x9617cdc4, "netif_receive_skb" },
	{ 0xce5ac24f, "zlib_inflate_workspacesize" },
	{ 0x75e8f29, "pci_bus_write_config_dword" },
	{ 0xd9f7b1a3, "mutex_lock" },
	{ 0xa34f1ef5, "crc32_le" },
	{ 0x521445b, "list_del" },
	{ 0xf3b39202, "mod_timer" },
	{ 0x1902adf, "netpoll_trap" },
	{ 0x5d868bf5, "dev_kfree_skb_any" },
	{ 0x284070b9, "contig_page_data" },
	{ 0x5feba87d, "dma_alloc_coherent" },
	{ 0xe523ad75, "synchronize_irq" },
	{ 0xcd58d944, "pci_find_capability" },
	{ 0x6b60eef6, "zlib_inflate" },
	{ 0x99bdf0a8, "cpu_online_map" },
	{ 0x929ef20f, "skb_over_panic" },
	{ 0x7dceceac, "capable" },
	{ 0x1451762f, "netif_device_attach" },
	{ 0x19070091, "kmem_cache_alloc" },
	{ 0x60758dc0, "__free_pages" },
	{ 0xd7aec53f, "netif_device_detach" },
	{ 0x3762cb6e, "ioremap_nocache" },
	{ 0xed7ec77b, "pci_bus_read_config_word" },
	{ 0x78e113b3, "ethtool_op_set_sg" },
	{ 0x423cae99, "pci_bus_read_config_dword" },
	{ 0x26e96637, "request_irq" },
	{ 0xd71dcda8, "kfree_skb" },
	{ 0x6b2dc060, "dump_stack" },
	{ 0x49cae68c, "eth_type_trans" },
	{ 0x348d8e49, "dev_driver_string" },
	{ 0x37264d0f, "pci_unregister_driver" },
	{ 0xcc5005fe, "msleep_interruptible" },
	{ 0x3b8be29d, "zlib_inflateInit2" },
	{ 0xd0b91f9b, "init_timer" },
	{ 0xe32e66b5, "_spin_unlock_bh" },
	{ 0xfcec0987, "enable_irq" },
	{ 0x9c55cec, "schedule_timeout_interruptible" },
	{ 0x37a0cba, "kfree" },
	{ 0x801678, "flush_scheduled_work" },
	{ 0xdb02d881, "pci_request_regions" },
	{ 0xedc03953, "iounmap" },
	{ 0xef3726d, "__pci_register_driver" },
	{ 0x6cf3e7c, "ethtool_op_get_tx_csum" },
	{ 0x37d0b921, "crc32c_le" },
	{ 0xfe760a52, "unregister_netdev" },
	{ 0x8a43757, "ethtool_op_get_tso" },
	{ 0x25da070, "snprintf" },
	{ 0xa58a0b02, "pci_choose_state" },
	{ 0x59e1ffc, "__netif_schedule" },
	{ 0x9a3de8f8, "csum_partial" },
	{ 0x6943ea4b, "_spin_lock_bh" },
	{ 0x94ef8c0d, "pci_enable_device" },
	{ 0x137bf5d6, "pci_set_consistent_dma_mask" },
	{ 0x6e720ff2, "rtnl_unlock" },
	{ 0x185a6335, "ethtool_op_get_perm_addr" },
	{ 0xfc2c5219, "__netif_rx_schedule" },
	{ 0xf20dabd8, "free_irq" },
	{ 0xd2212f52, "pci_save_state" },
};

static const char __module_depends[]
__attribute_used__
__attribute__((section(".modinfo"))) =
"depends=";

MODULE_ALIAS("pci:v000014E4d0000164Esv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000014E4d0000164Fsv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000014E4d00001650sv*sd*bc*sc*i*");

MODULE_INFO(srcversion, "8502510553B1C2243D9AE59");
