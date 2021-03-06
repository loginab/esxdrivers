/*******************************************************************************

  Intel(R) Gigabit Ethernet Linux driver
  Copyright(c) 2007-2008 Intel Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Contact Information:
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

/* e1000_82575
 * e1000_82576
 */

#include "e1000_api.h"

static s32  e1000_init_phy_params_82575(struct e1000_hw *hw);
static s32  e1000_init_nvm_params_82575(struct e1000_hw *hw);
static s32  e1000_init_mac_params_82575(struct e1000_hw *hw);
static s32  e1000_acquire_phy_82575(struct e1000_hw *hw);
static void e1000_release_phy_82575(struct e1000_hw *hw);
static s32  e1000_acquire_nvm_82575(struct e1000_hw *hw);
static void e1000_release_nvm_82575(struct e1000_hw *hw);
static s32  e1000_check_for_link_82575(struct e1000_hw *hw);
static s32  e1000_get_cfg_done_82575(struct e1000_hw *hw);
static s32  e1000_get_link_up_info_82575(struct e1000_hw *hw, u16 *speed,
                                         u16 *duplex);
static s32  e1000_init_hw_82575(struct e1000_hw *hw);
static s32  e1000_phy_hw_reset_sgmii_82575(struct e1000_hw *hw);
static s32  e1000_read_phy_reg_sgmii_82575(struct e1000_hw *hw, u32 offset,
                                           u16 *data);
static s32  e1000_reset_hw_82575(struct e1000_hw *hw);
static s32  e1000_set_d0_lplu_state_82575(struct e1000_hw *hw,
                                          bool active);
static s32  e1000_setup_copper_link_82575(struct e1000_hw *hw);
static s32  e1000_setup_fiber_serdes_link_82575(struct e1000_hw *hw);
static s32  e1000_valid_led_default_82575(struct e1000_hw *hw, u16 *data);
static s32  e1000_write_phy_reg_sgmii_82575(struct e1000_hw *hw,
                                            u32 offset, u16 data);
static void e1000_clear_hw_cntrs_82575(struct e1000_hw *hw);
static s32  e1000_acquire_swfw_sync_82575(struct e1000_hw *hw, u16 mask);
static s32  e1000_configure_pcs_link_82575(struct e1000_hw *hw);
static s32  e1000_get_pcs_speed_and_duplex_82575(struct e1000_hw *hw,
                                                 u16 *speed, u16 *duplex);
static s32  e1000_get_phy_id_82575(struct e1000_hw *hw);
static void e1000_release_swfw_sync_82575(struct e1000_hw *hw, u16 mask);
static bool e1000_sgmii_active_82575(struct e1000_hw *hw);
static s32  e1000_reset_init_script_82575(struct e1000_hw *hw);
static s32  e1000_read_mac_addr_82575(struct e1000_hw *hw);
static void e1000_power_down_phy_copper_82575(struct e1000_hw *hw);

static void e1000_init_rx_addrs_82575(struct e1000_hw *hw, u16 rar_count);
static void e1000_update_mc_addr_list_82575(struct e1000_hw *hw,
                                           u8 *mc_addr_list, u32 mc_addr_count,
                                           u32 rar_used_count, u32 rar_count);
void e1000_remove_device_82575(struct e1000_hw *hw);
void e1000_shutdown_fiber_serdes_link_82575(struct e1000_hw *hw);

struct e1000_dev_spec_82575 {
	bool sgmii_active;
};

/**
 *  e1000_init_phy_params_82575 - Init PHY func ptrs.
 *  @hw: pointer to the HW structure
 *
 *  This is a function pointer entry point called by the api module.
 **/
static s32 e1000_init_phy_params_82575(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val = E1000_SUCCESS;

	DEBUGFUNC("e1000_init_phy_params_82575");

	if (hw->phy.media_type != e1000_media_type_copper) {
		phy->type = e1000_phy_none;
		goto out;
	} else {
		phy->ops.power_up   = e1000_power_up_phy_copper;
		phy->ops.power_down = e1000_power_down_phy_copper_82575;
	}

	phy->autoneg_mask           = AUTONEG_ADVERTISE_SPEED_DEFAULT;
	phy->reset_delay_us         = 100;

	phy->ops.acquire            = e1000_acquire_phy_82575;
	phy->ops.check_reset_block  = e1000_check_reset_block_generic;
	phy->ops.commit             = e1000_phy_sw_reset_generic;
	phy->ops.get_cfg_done       = e1000_get_cfg_done_82575;
	phy->ops.release            = e1000_release_phy_82575;

	if (e1000_sgmii_active_82575(hw)) {
		phy->ops.reset      = e1000_phy_hw_reset_sgmii_82575;
		phy->ops.read_reg   = e1000_read_phy_reg_sgmii_82575;
		phy->ops.write_reg  = e1000_write_phy_reg_sgmii_82575;
	} else {
		phy->ops.reset      = e1000_phy_hw_reset_generic;
		phy->ops.read_reg   = e1000_read_phy_reg_igp;
		phy->ops.write_reg  = e1000_write_phy_reg_igp;
	}

	/* Set phy->phy_addr and phy->id. */
	ret_val = e1000_get_phy_id_82575(hw);

	/* Verify phy id and set remaining function pointers */
	switch (phy->id) {
	case M88E1111_I_PHY_ID:
		phy->type                   = e1000_phy_m88;
		phy->ops.check_polarity     = e1000_check_polarity_m88;
		phy->ops.get_info           = e1000_get_phy_info_m88;
		phy->ops.get_cable_length   = e1000_get_cable_length_m88;
		phy->ops.force_speed_duplex = e1000_phy_force_speed_duplex_m88;
		break;
	case IGP03E1000_E_PHY_ID:
	case IGP04E1000_E_PHY_ID:
		phy->type                   = e1000_phy_igp_3;
		phy->ops.check_polarity     = e1000_check_polarity_igp;
		phy->ops.get_info           = e1000_get_phy_info_igp;
		phy->ops.get_cable_length   = e1000_get_cable_length_igp_2;
		phy->ops.force_speed_duplex = e1000_phy_force_speed_duplex_igp;
		phy->ops.set_d0_lplu_state  = e1000_set_d0_lplu_state_82575;
		phy->ops.set_d3_lplu_state  = e1000_set_d3_lplu_state_generic;
		break;
	default:
		ret_val = -E1000_ERR_PHY;
		goto out;
	}

out:
	return ret_val;
}

/**
 *  e1000_init_nvm_params_82575 - Init NVM func ptrs.
 *  @hw: pointer to the HW structure
 *
 *  This is a function pointer entry point called by the api module.
 **/
static s32 e1000_init_nvm_params_82575(struct e1000_hw *hw)
{
	struct e1000_nvm_info *nvm = &hw->nvm;
	u32 eecd = E1000_READ_REG(hw, E1000_EECD);
	u16 size;

	DEBUGFUNC("e1000_init_nvm_params_82575");

	nvm->opcode_bits        = 8;
	nvm->delay_usec         = 1;
	switch (nvm->override) {
	case e1000_nvm_override_spi_large:
		nvm->page_size    = 32;
		nvm->address_bits = 16;
		break;
	case e1000_nvm_override_spi_small:
		nvm->page_size    = 8;
		nvm->address_bits = 8;
		break;
	default:
		nvm->page_size    = eecd & E1000_EECD_ADDR_BITS ? 32 : 8;
		nvm->address_bits = eecd & E1000_EECD_ADDR_BITS ? 16 : 8;
		break;
	}

	nvm->type              = e1000_nvm_eeprom_spi;

	size = (u16)((eecd & E1000_EECD_SIZE_EX_MASK) >>
	                  E1000_EECD_SIZE_EX_SHIFT);

	/*
	 * Added to a constant, "size" becomes the left-shift value
	 * for setting word_size.
	 */
	size += NVM_WORD_SIZE_BASE_SHIFT;

	/* EEPROM access above 16k is unsupported */
	if (size > 14)
		size = 14;
	nvm->word_size	= 1 << size;

	/* Function Pointers */
	nvm->ops.acquire       = e1000_acquire_nvm_82575;
	nvm->ops.read          = e1000_read_nvm_eerd;
	nvm->ops.release       = e1000_release_nvm_82575;
	nvm->ops.update        = e1000_update_nvm_checksum_generic;
	nvm->ops.valid_led_default = e1000_valid_led_default_82575;
	nvm->ops.validate      = e1000_validate_nvm_checksum_generic;
	nvm->ops.write         = e1000_write_nvm_spi;

	return E1000_SUCCESS;
}

/**
 *  e1000_init_mac_params_82575 - Init MAC func ptrs.
 *  @hw: pointer to the HW structure
 *
 *  This is a function pointer entry point called by the api module.
 **/
static s32 e1000_init_mac_params_82575(struct e1000_hw *hw)
{
	struct e1000_mac_info *mac = &hw->mac;
	struct e1000_dev_spec_82575 *dev_spec;
	u32 ctrl_ext = 0;
	s32 ret_val = E1000_SUCCESS;

	DEBUGFUNC("e1000_init_mac_params_82575");

	hw->dev_spec_size = sizeof(struct e1000_dev_spec_82575);

	/* Device-specific structure allocation */
	ret_val = e1000_alloc_zeroed_dev_spec_struct(hw, hw->dev_spec_size);
	if (ret_val)
		goto out;

	dev_spec = (struct e1000_dev_spec_82575 *)hw->dev_spec;

	/* Set media type */
        /*
	 * The 82575 uses bits 22:23 for link mode. The mode can be changed
         * based on the EEPROM. We cannot rely upon device ID. There
         * is no distinguishable difference between fiber and internal
         * SerDes mode on the 82575. There can be an external PHY attached
         * on the SGMII interface. For this, we'll set sgmii_active to true.
         */
	hw->phy.media_type = e1000_media_type_copper;
	dev_spec->sgmii_active = false;

	ctrl_ext = E1000_READ_REG(hw, E1000_CTRL_EXT);
	if ((ctrl_ext & E1000_CTRL_EXT_LINK_MODE_MASK) ==
	    E1000_CTRL_EXT_LINK_MODE_PCIE_SERDES) {
		hw->phy.media_type = e1000_media_type_internal_serdes;
		ctrl_ext |= E1000_CTRL_I2C_ENA;
	} else if (ctrl_ext & E1000_CTRL_EXT_LINK_MODE_SGMII) {
		dev_spec->sgmii_active = true;
		ctrl_ext |= E1000_CTRL_I2C_ENA;
	} else {
		ctrl_ext &= ~E1000_CTRL_I2C_ENA;
	}
	E1000_WRITE_REG(hw, E1000_CTRL_EXT, ctrl_ext);

	/* Set mta register count */
	mac->mta_reg_count = 128;
	/* Set rar entry count */
	mac->rar_entry_count = E1000_RAR_ENTRIES_82575;
	if (mac->type == e1000_82576)
		mac->rar_entry_count = E1000_RAR_ENTRIES_82576;
	/* Set if part includes ASF firmware */
	mac->asf_firmware_present = true;
	/* Set if manageability features are enabled. */
	mac->arc_subsystem_valid =
	        (E1000_READ_REG(hw, E1000_FWSM) & E1000_FWSM_MODE_MASK)
	                ? true : false;

	/* Function pointers */

	/* bus type/speed/width */
	mac->ops.get_bus_info = e1000_get_bus_info_pcie_generic;
	/* reset */
	mac->ops.reset_hw = e1000_reset_hw_82575;
	/* hw initialization */
	mac->ops.init_hw = e1000_init_hw_82575;
	/* link setup */
	mac->ops.setup_link = e1000_setup_link_generic;
	/* physical interface link setup */
	mac->ops.setup_physical_interface =
	        (hw->phy.media_type == e1000_media_type_copper)
	                ? e1000_setup_copper_link_82575
	                : e1000_setup_fiber_serdes_link_82575;
	/* physical interface shutdown */
	mac->ops.shutdown_serdes = e1000_shutdown_fiber_serdes_link_82575;
	/* check for link */
	mac->ops.check_for_link = e1000_check_for_link_82575;
	/* receive address register setting */
	mac->ops.rar_set = e1000_rar_set_generic;
	/* read mac address */
	mac->ops.read_mac_addr = e1000_read_mac_addr_82575;
	/* multicast address update */
	mac->ops.update_mc_addr_list = e1000_update_mc_addr_list_82575;
	/* writing VFTA */
	mac->ops.write_vfta = e1000_write_vfta_generic;
	/* clearing VFTA */
	mac->ops.clear_vfta = e1000_clear_vfta_generic;
	/* setting MTA */
	mac->ops.mta_set = e1000_mta_set_generic;
	/* blink LED */
	mac->ops.blink_led = e1000_blink_led_generic;
	/* setup LED */
	mac->ops.setup_led = e1000_setup_led_generic;
	/* cleanup LED */
	mac->ops.cleanup_led = e1000_cleanup_led_generic;
	/* turn on/off LED */
	mac->ops.led_on = e1000_led_on_generic;
	mac->ops.led_off = e1000_led_off_generic;
	/* remove device */
	mac->ops.remove_device = e1000_remove_device_82575;
	/* clear hardware counters */
	mac->ops.clear_hw_cntrs = e1000_clear_hw_cntrs_82575;
	/* link info */
	mac->ops.get_link_up_info = e1000_get_link_up_info_82575;

out:
	return ret_val;
}

/**
 *  e1000_init_function_pointers_82575 - Init func ptrs.
 *  @hw: pointer to the HW structure
 *
 *  The only function explicitly called by the api module to initialize
 *  all function pointers and parameters.
 **/
void e1000_init_function_pointers_82575(struct e1000_hw *hw)
{
	DEBUGFUNC("e1000_init_function_pointers_82575");

	hw->mac.ops.init_params = e1000_init_mac_params_82575;
	hw->nvm.ops.init_params = e1000_init_nvm_params_82575;
	hw->phy.ops.init_params = e1000_init_phy_params_82575;
}

/**
 *  e1000_acquire_phy_82575 - Acquire rights to access PHY
 *  @hw: pointer to the HW structure
 *
 *  Acquire access rights to the correct PHY.  This is a
 *  function pointer entry point called by the api module.
 **/
static s32 e1000_acquire_phy_82575(struct e1000_hw *hw)
{
	u16 mask;

	DEBUGFUNC("e1000_acquire_phy_82575");

	mask = hw->bus.func ? E1000_SWFW_PHY1_SM : E1000_SWFW_PHY0_SM;

	return e1000_acquire_swfw_sync_82575(hw, mask);
}

/**
 *  e1000_release_phy_82575 - Release rights to access PHY
 *  @hw: pointer to the HW structure
 *
 *  A wrapper to release access rights to the correct PHY.  This is a
 *  function pointer entry point called by the api module.
 **/
static void e1000_release_phy_82575(struct e1000_hw *hw)
{
	u16 mask;

	DEBUGFUNC("e1000_release_phy_82575");

	mask = hw->bus.func ? E1000_SWFW_PHY1_SM : E1000_SWFW_PHY0_SM;
	e1000_release_swfw_sync_82575(hw, mask);
}

/**
 *  e1000_read_phy_reg_sgmii_82575 - Read PHY register using sgmii
 *  @hw: pointer to the HW structure
 *  @offset: register offset to be read
 *  @data: pointer to the read data
 *
 *  Reads the PHY register at offset using the serial gigabit media independent
 *  interface and stores the retrieved information in data.
 **/
static s32 e1000_read_phy_reg_sgmii_82575(struct e1000_hw *hw, u32 offset,
                                          u16 *data)
{
	struct e1000_phy_info *phy = &hw->phy;
	u32 i, i2ccmd = 0;

	DEBUGFUNC("e1000_read_phy_reg_sgmii_82575");

	if (offset > E1000_MAX_SGMII_PHY_REG_ADDR) {
		DEBUGOUT1("PHY Address %u is out of range\n", offset);
		return -E1000_ERR_PARAM;
	}

	/*
	 * Set up Op-code, Phy Address, and register address in the I2CCMD
	 * register.  The MAC will take care of interfacing with the
	 * PHY to retrieve the desired data.
	 */
	i2ccmd = ((offset << E1000_I2CCMD_REG_ADDR_SHIFT) |
	          (phy->addr << E1000_I2CCMD_PHY_ADDR_SHIFT) |
	          (E1000_I2CCMD_OPCODE_READ));

	E1000_WRITE_REG(hw, E1000_I2CCMD, i2ccmd);

	/* Poll the ready bit to see if the I2C read completed */
	for (i = 0; i < E1000_I2CCMD_PHY_TIMEOUT; i++) {
		usec_delay(50);
		i2ccmd = E1000_READ_REG(hw, E1000_I2CCMD);
		if (i2ccmd & E1000_I2CCMD_READY)
			break;
	}
	if (!(i2ccmd & E1000_I2CCMD_READY)) {
		DEBUGOUT("I2CCMD Read did not complete\n");
		return -E1000_ERR_PHY;
	}
	if (i2ccmd & E1000_I2CCMD_ERROR) {
		DEBUGOUT("I2CCMD Error bit set\n");
		return -E1000_ERR_PHY;
	}

	/* Need to byte-swap the 16-bit value. */
	*data = ((i2ccmd >> 8) & 0x00FF) | ((i2ccmd << 8) & 0xFF00);

	return E1000_SUCCESS;
}

/**
 *  e1000_write_phy_reg_sgmii_82575 - Write PHY register using sgmii
 *  @hw: pointer to the HW structure
 *  @offset: register offset to write to
 *  @data: data to write at register offset
 *
 *  Writes the data to PHY register at the offset using the serial gigabit
 *  media independent interface.
 **/
static s32 e1000_write_phy_reg_sgmii_82575(struct e1000_hw *hw, u32 offset,
                                           u16 data)
{
	struct e1000_phy_info *phy = &hw->phy;
	u32 i, i2ccmd = 0;
	u16 phy_data_swapped;

	DEBUGFUNC("e1000_write_phy_reg_sgmii_82575");

	if (offset > E1000_MAX_SGMII_PHY_REG_ADDR) {
		DEBUGOUT1("PHY Address %d is out of range\n", offset);
		return -E1000_ERR_PARAM;
	}

	/* Swap the data bytes for the I2C interface */
	phy_data_swapped = ((data >> 8) & 0x00FF) | ((data << 8) & 0xFF00);

	/*
	 * Set up Op-code, Phy Address, and register address in the I2CCMD
	 * register.  The MAC will take care of interfacing with the
	 * PHY to retrieve the desired data.
	 */
	i2ccmd = ((offset << E1000_I2CCMD_REG_ADDR_SHIFT) |
	          (phy->addr << E1000_I2CCMD_PHY_ADDR_SHIFT) |
	          E1000_I2CCMD_OPCODE_WRITE |
	          phy_data_swapped);

	E1000_WRITE_REG(hw, E1000_I2CCMD, i2ccmd);

	/* Poll the ready bit to see if the I2C read completed */
	for (i = 0; i < E1000_I2CCMD_PHY_TIMEOUT; i++) {
		usec_delay(50);
		i2ccmd = E1000_READ_REG(hw, E1000_I2CCMD);
		if (i2ccmd & E1000_I2CCMD_READY)
			break;
	}
	if (!(i2ccmd & E1000_I2CCMD_READY)) {
		DEBUGOUT("I2CCMD Write did not complete\n");
		return -E1000_ERR_PHY;
	}
	if (i2ccmd & E1000_I2CCMD_ERROR) {
		DEBUGOUT("I2CCMD Error bit set\n");
		return -E1000_ERR_PHY;
	}

	return E1000_SUCCESS;
}

/**
 *  e1000_get_phy_id_82575 - Retrieve PHY addr and id
 *  @hw: pointer to the HW structure
 *
 *  Retrieves the PHY address and ID for both PHY's which do and do not use
 *  sgmi interface.
 **/
static s32 e1000_get_phy_id_82575(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32  ret_val = E1000_SUCCESS;
	u16 phy_id;

	DEBUGFUNC("e1000_get_phy_id_82575");

	/*
	 * For SGMII PHYs, we try the list of possible addresses until
	 * we find one that works.  For non-SGMII PHYs
	 * (e.g. integrated copper PHYs), an address of 1 should
	 * work.  The result of this function should mean phy->phy_addr
	 * and phy->id are set correctly.
	 */
	if (!(e1000_sgmii_active_82575(hw))) {
		phy->addr = 1;
		ret_val = e1000_get_phy_id(hw);
		goto out;
	}

	/*
	 * The address field in the I2CCMD register is 3 bits and 0 is invalid.
	 * Therefore, we need to test 1-7
	 */
	for (phy->addr = 1; phy->addr < 8; phy->addr++) {
		ret_val = e1000_read_phy_reg_sgmii_82575(hw, PHY_ID1, &phy_id);
		if (ret_val == E1000_SUCCESS) {
			DEBUGOUT2("Vendor ID 0x%08X read at address %u\n",
			          phy_id,
			          phy->addr);
			/*
			 * At the time of this writing, The M88 part is
			 * the only supported SGMII PHY product.
			 */
			if (phy_id == M88_VENDOR)
				break;
		} else {
			DEBUGOUT1("PHY address %u was unreadable\n",
			          phy->addr);
		}
	}

	/* A valid PHY type couldn't be found. */
	if (phy->addr == 8) {
		phy->addr = 0;
		ret_val = -E1000_ERR_PHY;
		goto out;
	}

	ret_val = e1000_get_phy_id(hw);

out:
	return ret_val;
}

/**
 *  e1000_phy_hw_reset_sgmii_82575 - Performs a PHY reset
 *  @hw: pointer to the HW structure
 *
 *  Resets the PHY using the serial gigabit media independent interface.
 **/
static s32 e1000_phy_hw_reset_sgmii_82575(struct e1000_hw *hw)
{
	s32 ret_val = E1000_SUCCESS;

	DEBUGFUNC("e1000_phy_hw_reset_sgmii_82575");

	/*
	 * This isn't a true "hard" reset, but is the only reset
	 * available to us at this time.
	 */

	DEBUGOUT("Soft resetting SGMII attached PHY...\n");

	if (!(hw->phy.ops.write_reg))
		goto out;

	/*
	 * SFP documentation requires the following to configure the SPF module
	 * to work on SGMII.  No further documentation is given.
	 */
	ret_val = hw->phy.ops.write_reg(hw, 0x1B, 0x8084);
	if (ret_val)
		goto out;

	ret_val = hw->phy.ops.commit(hw);

out:
	return ret_val;
}

/**
 *  e1000_set_d0_lplu_state_82575 - Set Low Power Linkup D0 state
 *  @hw: pointer to the HW structure
 *  @active: true to enable LPLU, false to disable
 *
 *  Sets the LPLU D0 state according to the active flag.  When
 *  activating LPLU this function also disables smart speed
 *  and vice versa.  LPLU will not be activated unless the
 *  device autonegotiation advertisement meets standards of
 *  either 10 or 10/100 or 10/100/1000 at all duplexes.
 *  This is a function pointer entry point only called by
 *  PHY setup routines.
 **/
static s32 e1000_set_d0_lplu_state_82575(struct e1000_hw *hw, bool active)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val = E1000_SUCCESS;
	u16 data;

	DEBUGFUNC("e1000_set_d0_lplu_state_82575");

	if (!(hw->phy.ops.read_reg))
		goto out;

	ret_val = phy->ops.read_reg(hw, IGP02E1000_PHY_POWER_MGMT, &data);
	if (ret_val)
		goto out;

	if (active) {
		data |= IGP02E1000_PM_D0_LPLU;
		ret_val = phy->ops.write_reg(hw, IGP02E1000_PHY_POWER_MGMT,
		                             data);
		if (ret_val)
			goto out;

		/* When LPLU is enabled, we should disable SmartSpeed */
		ret_val = phy->ops.read_reg(hw, IGP01E1000_PHY_PORT_CONFIG,
		                            &data);
		data &= ~IGP01E1000_PSCFR_SMART_SPEED;
		ret_val = phy->ops.write_reg(hw, IGP01E1000_PHY_PORT_CONFIG,
		                             data);
		if (ret_val)
			goto out;
	} else {
		data &= ~IGP02E1000_PM_D0_LPLU;
		ret_val = phy->ops.write_reg(hw, IGP02E1000_PHY_POWER_MGMT,
		                             data);
		/*
		 * LPLU and SmartSpeed are mutually exclusive.  LPLU is used
		 * during Dx states where the power conservation is most
		 * important.  During driver activity we should enable
		 * SmartSpeed, so performance is maintained.
		 */
		if (phy->smart_speed == e1000_smart_speed_on) {
			ret_val = phy->ops.read_reg(hw,
			                            IGP01E1000_PHY_PORT_CONFIG,
			                            &data);
			if (ret_val)
				goto out;

			data |= IGP01E1000_PSCFR_SMART_SPEED;
			ret_val = phy->ops.write_reg(hw,
			                             IGP01E1000_PHY_PORT_CONFIG,
			                             data);
			if (ret_val)
				goto out;
		} else if (phy->smart_speed == e1000_smart_speed_off) {
			ret_val = phy->ops.read_reg(hw,
			                            IGP01E1000_PHY_PORT_CONFIG,
			                            &data);
			if (ret_val)
				goto out;

			data &= ~IGP01E1000_PSCFR_SMART_SPEED;
			ret_val = phy->ops.write_reg(hw,
			                             IGP01E1000_PHY_PORT_CONFIG,
			                             data);
			if (ret_val)
				goto out;
		}
	}

out:
	return ret_val;
}

/**
 *  e1000_acquire_nvm_82575 - Request for access to EEPROM
 *  @hw: pointer to the HW structure
 *
 *  Acquire the necessary semaphores for exclusive access to the EEPROM.
 *  Set the EEPROM access request bit and wait for EEPROM access grant bit.
 *  Return successful if access grant bit set, else clear the request for
 *  EEPROM access and return -E1000_ERR_NVM (-1).
 **/
static s32 e1000_acquire_nvm_82575(struct e1000_hw *hw)
{
	s32 ret_val;

	DEBUGFUNC("e1000_acquire_nvm_82575");

	ret_val = e1000_acquire_swfw_sync_82575(hw, E1000_SWFW_EEP_SM);
	if (ret_val)
		goto out;

	ret_val = e1000_acquire_nvm_generic(hw);

	if (ret_val)
		e1000_release_swfw_sync_82575(hw, E1000_SWFW_EEP_SM);

out:
	return ret_val;
}

/**
 *  e1000_release_nvm_82575 - Release exclusive access to EEPROM
 *  @hw: pointer to the HW structure
 *
 *  Stop any current commands to the EEPROM and clear the EEPROM request bit,
 *  then release the semaphores acquired.
 **/
static void e1000_release_nvm_82575(struct e1000_hw *hw)
{
	DEBUGFUNC("e1000_release_nvm_82575");

	e1000_release_nvm_generic(hw);
	e1000_release_swfw_sync_82575(hw, E1000_SWFW_EEP_SM);
}

/**
 *  e1000_acquire_swfw_sync_82575 - Acquire SW/FW semaphore
 *  @hw: pointer to the HW structure
 *  @mask: specifies which semaphore to acquire
 *
 *  Acquire the SW/FW semaphore to access the PHY or NVM.  The mask
 *  will also specify which port we're acquiring the lock for.
 **/
static s32 e1000_acquire_swfw_sync_82575(struct e1000_hw *hw, u16 mask)
{
	u32 swfw_sync;
	u32 swmask = mask;
	u32 fwmask = mask << 16;
	s32 ret_val = E1000_SUCCESS;
	s32 i = 0, timeout = 200; /* FIXME: find real value to use here */

	DEBUGFUNC("e1000_acquire_swfw_sync_82575");

	while (i < timeout) {
		if (e1000_get_hw_semaphore_generic(hw)) {
			ret_val = -E1000_ERR_SWFW_SYNC;
			goto out;
		}

		swfw_sync = E1000_READ_REG(hw, E1000_SW_FW_SYNC);
		if (!(swfw_sync & (fwmask | swmask)))
			break;

		/*
		 * Firmware currently using resource (fwmask)
		 * or other software thread using resource (swmask)
		 */
		e1000_put_hw_semaphore_generic(hw);
		msec_delay_irq(5);
		i++;
	}

	if (i == timeout) {
		DEBUGOUT("Driver can't access resource, SW_FW_SYNC timeout.\n");
		ret_val = -E1000_ERR_SWFW_SYNC;
		goto out;
	}

	swfw_sync |= swmask;
	E1000_WRITE_REG(hw, E1000_SW_FW_SYNC, swfw_sync);

	e1000_put_hw_semaphore_generic(hw);

out:
	return ret_val;
}

/**
 *  e1000_release_swfw_sync_82575 - Release SW/FW semaphore
 *  @hw: pointer to the HW structure
 *  @mask: specifies which semaphore to acquire
 *
 *  Release the SW/FW semaphore used to access the PHY or NVM.  The mask
 *  will also specify which port we're releasing the lock for.
 **/
static void e1000_release_swfw_sync_82575(struct e1000_hw *hw, u16 mask)
{
	u32 swfw_sync;

	DEBUGFUNC("e1000_release_swfw_sync_82575");

	while (e1000_get_hw_semaphore_generic(hw) != E1000_SUCCESS);
	/* Empty */

	swfw_sync = E1000_READ_REG(hw, E1000_SW_FW_SYNC);
	swfw_sync &= ~mask;
	E1000_WRITE_REG(hw, E1000_SW_FW_SYNC, swfw_sync);

	e1000_put_hw_semaphore_generic(hw);
}

/**
 *  e1000_get_cfg_done_82575 - Read config done bit
 *  @hw: pointer to the HW structure
 *
 *  Read the management control register for the config done bit for
 *  completion status.  NOTE: silicon which is EEPROM-less will fail trying
 *  to read the config done bit, so an error is *ONLY* logged and returns
 *  E1000_SUCCESS.  If we were to return with error, EEPROM-less silicon
 *  would not be able to be reset or change link.
 **/
static s32 e1000_get_cfg_done_82575(struct e1000_hw *hw)
{
	s32 timeout = PHY_CFG_TIMEOUT;
	s32 ret_val = E1000_SUCCESS;
	u32 mask = E1000_NVM_CFG_DONE_PORT_0;

	DEBUGFUNC("e1000_get_cfg_done_82575");

	if (hw->bus.func == 1)
		mask = E1000_NVM_CFG_DONE_PORT_1;

	while (timeout) {
		if (E1000_READ_REG(hw, E1000_EEMNGCTL) & mask)
			break;
		msec_delay(1);
		timeout--;
	}
	if (!timeout) {
		DEBUGOUT("MNG configuration cycle has not completed.\n");
	}

	/* If EEPROM is not marked present, init the PHY manually */
	if (((E1000_READ_REG(hw, E1000_EECD) & E1000_EECD_PRES) == 0) &&
	    (hw->phy.type == e1000_phy_igp_3)) {
		e1000_phy_init_script_igp3(hw);
	}

	return ret_val;
}

/**
 *  e1000_get_link_up_info_82575 - Get link speed/duplex info
 *  @hw: pointer to the HW structure
 *  @speed: stores the current speed
 *  @duplex: stores the current duplex
 *
 *  This is a wrapper function, if using the serial gigabit media independent
 *  interface, use PCS to retrieve the link speed and duplex information.
 *  Otherwise, use the generic function to get the link speed and duplex info.
 **/
static s32 e1000_get_link_up_info_82575(struct e1000_hw *hw, u16 *speed,
                                        u16 *duplex)
{
	s32 ret_val;

	DEBUGFUNC("e1000_get_link_up_info_82575");

	if (hw->phy.media_type != e1000_media_type_copper ||
	    e1000_sgmii_active_82575(hw)) {
		ret_val = e1000_get_pcs_speed_and_duplex_82575(hw, speed,
		                                               duplex);
	} else {
		ret_val = e1000_get_speed_and_duplex_copper_generic(hw, speed,
		                                                    duplex);
	}

	return ret_val;
}

/**
 *  e1000_check_for_link_82575 - Check for link
 *  @hw: pointer to the HW structure
 *
 *  If sgmii is enabled, then use the pcs register to determine link, otherwise
 *  use the generic interface for determining link.
 **/
static s32 e1000_check_for_link_82575(struct e1000_hw *hw)
{
	s32 ret_val;
	u16 speed, duplex;

	DEBUGFUNC("e1000_check_for_link_82575");

	/* SGMII link check is done through the PCS register. */
	if ((hw->phy.media_type != e1000_media_type_copper) ||
	    (e1000_sgmii_active_82575(hw)))
		ret_val = e1000_get_pcs_speed_and_duplex_82575(hw, &speed,
		                                               &duplex);
	else
		ret_val = e1000_check_for_copper_link_generic(hw);

	return ret_val;
}

/**
 *  e1000_get_pcs_speed_and_duplex_82575 - Retrieve current speed/duplex
 *  @hw: pointer to the HW structure
 *  @speed: stores the current speed
 *  @duplex: stores the current duplex
 *
 *  Using the physical coding sub-layer (PCS), retrieve the current speed and
 *  duplex, then store the values in the pointers provided.
 **/
static s32 e1000_get_pcs_speed_and_duplex_82575(struct e1000_hw *hw,
                                                u16 *speed, u16 *duplex)
{
	struct e1000_mac_info *mac = &hw->mac;
	u32 pcs;

	DEBUGFUNC("e1000_get_pcs_speed_and_duplex_82575");

	/* Set up defaults for the return values of this function */
	mac->serdes_has_link = false;
	*speed = 0;
	*duplex = 0;

	/*
	 * Read the PCS Status register for link state. For non-copper mode,
	 * the status register is not accurate. The PCS status register is
	 * used instead.
	 */
	pcs = E1000_READ_REG(hw, E1000_PCS_LSTAT);

	/*
	 * The link up bit determines when link is up on autoneg. The sync ok
	 * gets set once both sides sync up and agree upon link. Stable link
	 * can be determined by checking for both link up and link sync ok
	 */
	if ((pcs & E1000_PCS_LSTS_LINK_OK) && (pcs & E1000_PCS_LSTS_SYNK_OK)) {
		mac->serdes_has_link = true;

		/* Detect and store PCS speed */
		if (pcs & E1000_PCS_LSTS_SPEED_1000) {
			*speed = SPEED_1000;
		} else if (pcs & E1000_PCS_LSTS_SPEED_100) {
			*speed = SPEED_100;
		} else {
			*speed = SPEED_10;
		}

		/* Detect and store PCS duplex */
		if (pcs & E1000_PCS_LSTS_DUPLEX_FULL) {
			*duplex = FULL_DUPLEX;
		} else {
			*duplex = HALF_DUPLEX;
		}
	}

	return E1000_SUCCESS;
}

/**
 *  e1000_init_rx_addrs_82575 - Initialize receive address's
 *  @hw: pointer to the HW structure
 *  @rar_count: receive address registers
 *
 *  Setups the receive address registers by setting the base receive address
 *  register to the devices MAC address and clearing all the other receive
 *  address registers to 0.
 **/
static void e1000_init_rx_addrs_82575(struct e1000_hw *hw, u16 rar_count)
{
	u32 i;
	u8 addr[6] = {0,0,0,0,0,0};
	/*
	 * This function is essentially the same as that of
	 * e1000_init_rx_addrs_generic. However it also takes care
	 * of the special case where the register offset of the
	 * second set of RARs begins elsewhere. This is implicitly taken care by
	 * function e1000_rar_set_generic.
	 */

	DEBUGFUNC("e1000_init_rx_addrs_82575");

	/* Setup the receive address */
	DEBUGOUT("Programming MAC Address into RAR[0]\n");
	hw->mac.ops.rar_set(hw, hw->mac.addr, 0);

	/* Zero out the other (rar_entry_count - 1) receive addresses */
	DEBUGOUT1("Clearing RAR[1-%u]\n", rar_count-1);
	for (i = 1; i < rar_count; i++) {
	    hw->mac.ops.rar_set(hw, addr, i);
	}
}

/**
 *  e1000_update_mc_addr_list_82575 - Update Multicast addresses
 *  @hw: pointer to the HW structure
 *  @mc_addr_list: array of multicast addresses to program
 *  @mc_addr_count: number of multicast addresses to program
 *  @rar_used_count: the first RAR register free to program
 *  @rar_count: total number of supported Receive Address Registers
 *
 *  Updates the Receive Address Registers and Multicast Table Array.
 *  The caller must have a packed mc_addr_list of multicast addresses.
 *  The parameter rar_count will usually be hw->mac.rar_entry_count
 *  unless there are workarounds that change this.
 **/
static void e1000_update_mc_addr_list_82575(struct e1000_hw *hw,
                                     u8 *mc_addr_list, u32 mc_addr_count,
                                     u32 rar_used_count, u32 rar_count)
{
	u32 hash_value;
	u32 i;
	u8 addr[6] = {0,0,0,0,0,0};
	/*
	 * This function is essentially the same as that of 
	 * e1000_update_mc_addr_list_generic. However it also takes care 
	 * of the special case where the register offset of the 
	 * second set of RARs begins elsewhere. This is implicitly taken care by 
	 * function e1000_rar_set_generic.
	 */

	DEBUGFUNC("e1000_update_mc_addr_list_82575");

	/*
	 * Load the first set of multicast addresses into the exact
	 * filters (RAR).  If there are not enough to fill the RAR
	 * array, clear the filters.
	 */
	for (i = rar_used_count; i < rar_count; i++) {
		if (mc_addr_count) {
			e1000_rar_set_generic(hw, mc_addr_list, i);
			mc_addr_count--;
			mc_addr_list += ETH_ADDR_LEN;
		} else {
			e1000_rar_set_generic(hw, addr, i);
		}
	}

	/* Clear the old settings from the MTA */
	DEBUGOUT("Clearing MTA\n");
	for (i = 0; i < hw->mac.mta_reg_count; i++) {
		E1000_WRITE_REG_ARRAY(hw, E1000_MTA, i, 0);
		E1000_WRITE_FLUSH(hw);
	}

	/* Load any remaining multicast addresses into the hash table. */
	for (; mc_addr_count > 0; mc_addr_count--) {
		hash_value = e1000_hash_mc_addr_generic(hw, mc_addr_list);
		DEBUGOUT1("Hash value = 0x%03X\n", hash_value);
		hw->mac.ops.mta_set(hw, hash_value);
		mc_addr_list += ETH_ADDR_LEN;
	}
}

/**
 *  e1000_shutdown_fiber_serdes_link_82575 - Remove link during power down
 *  @hw: pointer to the HW structure
 *
 *  In the case of fiber serdes shut down optics and PCS on driver unload
 *  when management pass thru is not enabled.
 **/
void e1000_shutdown_fiber_serdes_link_82575(struct e1000_hw *hw)
{
	u32 reg;

	if (hw->mac.type != e1000_82576 ||
	   (hw->phy.media_type != e1000_media_type_fiber &&
	    hw->phy.media_type != e1000_media_type_internal_serdes))
		return;

	/* if the management interface is not enabled, then power down */
	if (!e1000_enable_mng_pass_thru(hw)) {
		/* Disable PCS to turn off link */
		reg = E1000_READ_REG(hw, E1000_PCS_CFG0);
		reg &= ~E1000_PCS_CFG_PCS_EN;
		E1000_WRITE_REG(hw, E1000_PCS_CFG0, reg);

		/* shutdown the laser */
		reg = E1000_READ_REG(hw, E1000_CTRL_EXT);
		reg |= E1000_CTRL_EXT_SDP7_DATA;
		E1000_WRITE_REG(hw, E1000_CTRL_EXT, reg);

		/* flush the write to verfiy completion */
		E1000_WRITE_FLUSH(hw);
		msec_delay(1);
	}

	return;
}

/**
 *  e1000_reset_hw_82575 - Reset hardware
 *  @hw: pointer to the HW structure
 *
 *  This resets the hardware into a known state.  This is a
 *  function pointer entry point called by the api module.
 **/
static s32 e1000_reset_hw_82575(struct e1000_hw *hw)
{
	u32 ctrl, icr;
	s32 ret_val;

	DEBUGFUNC("e1000_reset_hw_82575");

	/*
	 * Prevent the PCI-E bus from sticking if there is no TLP connection
	 * on the last TLP read/write transaction when MAC is reset.
	 */
	ret_val = e1000_disable_pcie_master_generic(hw);
	if (ret_val) {
		DEBUGOUT("PCI-E Master disable polling has failed.\n");
	}

	DEBUGOUT("Masking off all interrupts\n");
	E1000_WRITE_REG(hw, E1000_IMC, 0xffffffff);

	E1000_WRITE_REG(hw, E1000_RCTL, 0);
	E1000_WRITE_REG(hw, E1000_TCTL, E1000_TCTL_PSP);
	E1000_WRITE_FLUSH(hw);

	msec_delay(10);

	ctrl = E1000_READ_REG(hw, E1000_CTRL);

	DEBUGOUT("Issuing a global reset to MAC\n");
	E1000_WRITE_REG(hw, E1000_CTRL, ctrl | E1000_CTRL_RST);

	ret_val = e1000_get_auto_rd_done_generic(hw);
	if (ret_val) {
		/*
		 * When auto config read does not complete, do not
		 * return with an error. This can happen in situations
		 * where there is no eeprom and prevents getting link.
		 */
		DEBUGOUT("Auto Read Done did not complete\n");
	}

	/* If EEPROM is not present, run manual init scripts */
	if ((E1000_READ_REG(hw, E1000_EECD) & E1000_EECD_PRES) == 0)
		e1000_reset_init_script_82575(hw);

	/* Clear any pending interrupt events. */
	E1000_WRITE_REG(hw, E1000_IMC, 0xffffffff);
	icr = E1000_READ_REG(hw, E1000_ICR);

	e1000_check_alt_mac_addr_generic(hw);

	return ret_val;
}

/**
 *  e1000_init_hw_82575 - Initialize hardware
 *  @hw: pointer to the HW structure
 *
 *  This inits the hardware readying it for operation.
 **/
static s32 e1000_init_hw_82575(struct e1000_hw *hw)
{
	struct e1000_mac_info *mac = &hw->mac;
	s32 ret_val;
	u16 i, rar_count = mac->rar_entry_count;

	DEBUGFUNC("e1000_init_hw_82575");

	/* Initialize identification LED */
	ret_val = e1000_id_led_init_generic(hw);
	if (ret_val) {
		DEBUGOUT("Error initializing identification LED\n");
		/* This is not fatal and we should not stop init due to this */
	}

	/* Disabling VLAN filtering */
	DEBUGOUT("Initializing the IEEE VLAN\n");
	mac->ops.clear_vfta(hw);

	/* Setup the receive address */
	e1000_init_rx_addrs_82575(hw, rar_count);
	/* Zero out the Multicast HASH table */
	DEBUGOUT("Zeroing the MTA\n");
	for (i = 0; i < mac->mta_reg_count; i++)
		E1000_WRITE_REG_ARRAY(hw, E1000_MTA, i, 0);

	/* Setup link and flow control */
	ret_val = mac->ops.setup_link(hw);

	/*
	 * Clear all of the statistics registers (clear on read).  It is
	 * important that we do this after we have tried to establish link
	 * because the symbol error count will increment wildly if there
	 * is no link.
	 */
	e1000_clear_hw_cntrs_82575(hw);

	return ret_val;
}

/**
 *  e1000_setup_copper_link_82575 - Configure copper link settings
 *  @hw: pointer to the HW structure
 *
 *  Configures the link for auto-neg or forced speed and duplex.  Then we check
 *  for link, once link is established calls to configure collision distance
 *  and flow control are called.
 **/
static s32 e1000_setup_copper_link_82575(struct e1000_hw *hw)
{
	u32 ctrl, led_ctrl;
	s32  ret_val;
	bool link;

	DEBUGFUNC("e1000_setup_copper_link_82575");

	ctrl = E1000_READ_REG(hw, E1000_CTRL);
	ctrl |= E1000_CTRL_SLU;
	ctrl &= ~(E1000_CTRL_FRCSPD | E1000_CTRL_FRCDPX);
	E1000_WRITE_REG(hw, E1000_CTRL, ctrl);

	switch (hw->phy.type) {
	case e1000_phy_m88:
		ret_val = e1000_copper_link_setup_m88(hw);
		break;
	case e1000_phy_igp_3:
		ret_val = e1000_copper_link_setup_igp(hw);
		/* Setup activity LED */
		led_ctrl = E1000_READ_REG(hw, E1000_LEDCTL);
		led_ctrl &= IGP_ACTIVITY_LED_MASK;
		led_ctrl |= (IGP_ACTIVITY_LED_ENABLE | IGP_LED3_MODE);
		E1000_WRITE_REG(hw, E1000_LEDCTL, led_ctrl);
		break;
	default:
		ret_val = -E1000_ERR_PHY;
		break;
	}

	if (ret_val)
		goto out;

	if (hw->mac.autoneg) {
		/*
		 * Setup autoneg and flow control advertisement
		 * and perform autonegotiation.
		 */
		ret_val = e1000_copper_link_autoneg(hw);
		if (ret_val)
			goto out;
	} else {
		/*
		 * PHY will be set to 10H, 10F, 100H or 100F
		 * depending on user settings.
		 */
		DEBUGOUT("Forcing Speed and Duplex\n");
		ret_val = hw->phy.ops.force_speed_duplex(hw);
		if (ret_val) {
			DEBUGOUT("Error Forcing Speed and Duplex\n");
			goto out;
		}
	}

	ret_val = e1000_configure_pcs_link_82575(hw);
	if (ret_val)
		goto out;

	/*
	 * Check link status. Wait up to 100 microseconds for link to become
	 * valid.
	 */
	ret_val = e1000_phy_has_link_generic(hw,
	                                     COPPER_LINK_UP_LIMIT,
	                                     10,
	                                     &link);
	if (ret_val)
		goto out;

	if (link) {
		DEBUGOUT("Valid link established!!!\n");
		/* Config the MAC and PHY after link is up */
		e1000_config_collision_dist_generic(hw);
		ret_val = e1000_config_fc_after_link_up_generic(hw);
	} else {
		DEBUGOUT("Unable to establish link!!!\n");
	}

out:
	return ret_val;
}

/**
 *  e1000_setup_fiber_serdes_link_82575 - Setup link for fiber/serdes
 *  @hw: pointer to the HW structure
 *
 *  Configures speed and duplex for fiber and serdes links.
 **/
static s32 e1000_setup_fiber_serdes_link_82575(struct e1000_hw *hw)
{
	u32 reg;

	DEBUGFUNC("e1000_setup_fiber_serdes_link_82575");

	/*
	 * On the 82575, SerDes loopback mode persists until it is
	 * explicitly turned off or a power cycle is performed.  A read to
	 * the register does not indicate its status.  Therefore, we ensure
	 * loopback mode is disabled during initialization.
	 */
	E1000_WRITE_REG(hw, E1000_SCTL, E1000_SCTL_DISABLE_SERDES_LOOPBACK);

	/* Force link up, set 1gb, set both sw defined pins */
	reg = E1000_READ_REG(hw, E1000_CTRL);
	reg |= E1000_CTRL_SLU |
	       E1000_CTRL_SPD_1000 |
	       E1000_CTRL_FRCSPD |
	       E1000_CTRL_SWDPIN0 |
	       E1000_CTRL_SWDPIN1;
	E1000_WRITE_REG(hw, E1000_CTRL, reg);

	/* Power on phy for 82576 fiber adapters */
	if (hw->mac.type == e1000_82576) {
		reg = E1000_READ_REG(hw, E1000_CTRL_EXT);
		reg &= ~E1000_CTRL_EXT_SDP7_DATA;
		E1000_WRITE_REG(hw, E1000_CTRL_EXT, reg);
	}

	/* Set switch control to serdes energy detect */
	reg = E1000_READ_REG(hw, E1000_CONNSW);
	reg |= E1000_CONNSW_ENRGSRC;
	E1000_WRITE_REG(hw, E1000_CONNSW, reg);

	/*
	 * New SerDes mode allows for forcing speed or autonegotiating speed
	 * at 1gb. Autoneg should be default set by most drivers. This is the
	 * mode that will be compatible with older link partners and switches.
	 * However, both are supported by the hardware and some drivers/tools.
	 */
	reg = E1000_READ_REG(hw, E1000_PCS_LCTL);

	reg &= ~(E1000_PCS_LCTL_AN_ENABLE | E1000_PCS_LCTL_FLV_LINK_UP |
		E1000_PCS_LCTL_FSD | E1000_PCS_LCTL_FORCE_LINK);

	if (hw->mac.autoneg) {
		/* Set PCS register for autoneg */
		reg |= E1000_PCS_LCTL_FSV_1000 |      /* Force 1000    */
		       E1000_PCS_LCTL_FDV_FULL |      /* SerDes Full duplex */
		       E1000_PCS_LCTL_AN_ENABLE |     /* Enable Autoneg */
		       E1000_PCS_LCTL_AN_RESTART;     /* Restart autoneg */
		DEBUGOUT1("Configuring Autoneg; PCS_LCTL = 0x%08X\n", reg);
	} else {
		/* Set PCS register for forced speed */
		reg |= E1000_PCS_LCTL_FLV_LINK_UP |   /* Force link up */
		       E1000_PCS_LCTL_FSV_1000 |      /* Force 1000    */
		       E1000_PCS_LCTL_FDV_FULL |      /* SerDes Full duplex */
		       E1000_PCS_LCTL_FSD |           /* Force Speed */
		       E1000_PCS_LCTL_FORCE_LINK;     /* Force Link */
		DEBUGOUT1("Configuring Forced Link; PCS_LCTL = 0x%08X\n", reg);
	}
	E1000_WRITE_REG(hw, E1000_PCS_LCTL, reg);

	return E1000_SUCCESS;
}

/**
 *  e1000_valid_led_default_82575 - Verify a valid default LED config
 *  @hw: pointer to the HW structure
 *  @data: pointer to the NVM (EEPROM)
 *
 *  Read the EEPROM for the current default LED configuration.  If the
 *  LED configuration is not valid, set to a valid LED configuration.
 **/
static s32 e1000_valid_led_default_82575(struct e1000_hw *hw, u16 *data)
{
	s32 ret_val;

	DEBUGFUNC("e1000_valid_led_default_82575");

	ret_val = hw->nvm.ops.read(hw, NVM_ID_LED_SETTINGS, 1, data);
	if (ret_val) {
		DEBUGOUT("NVM Read Error\n");
		goto out;
	}

	if (*data == ID_LED_RESERVED_0000 || *data == ID_LED_RESERVED_FFFF) {
		switch(hw->phy.media_type) {
		case e1000_media_type_fiber:
		case e1000_media_type_internal_serdes:
			*data = ID_LED_DEFAULT_82575_SERDES;
			break;
		case e1000_media_type_copper:
		default:
			*data = ID_LED_DEFAULT;
			break;
		}
	}
out:
	return ret_val;
}

/**
 *  e1000_configure_pcs_link_82575 - Configure PCS link
 *  @hw: pointer to the HW structure
 *
 *  Configure the physical coding sub-layer (PCS) link.  The PCS link is
 *  only used on copper connections where the serialized gigabit media
 *  independent interface (sgmii) is being used.  Configures the link
 *  for auto-negotiation or forces speed/duplex.
 **/
static s32 e1000_configure_pcs_link_82575(struct e1000_hw *hw)
{
	struct e1000_mac_info *mac = &hw->mac;
	u32 reg = 0;

	DEBUGFUNC("e1000_configure_pcs_link_82575");

	if (hw->phy.media_type != e1000_media_type_copper ||
	    !(e1000_sgmii_active_82575(hw)))
		goto out;

	/* For SGMII, we need to issue a PCS autoneg restart */
	reg = E1000_READ_REG(hw, E1000_PCS_LCTL);

	/* AN time out should be disabled for SGMII mode */
	reg &= ~(E1000_PCS_LCTL_AN_TIMEOUT);

	if (mac->autoneg) {
		/* Make sure forced speed and force link are not set */
		reg &= ~(E1000_PCS_LCTL_FSD | E1000_PCS_LCTL_FORCE_LINK);

		/*
		 * The PHY should be setup prior to calling this function.
		 * All we need to do is restart autoneg and enable autoneg.
		 */
		reg |= E1000_PCS_LCTL_AN_RESTART | E1000_PCS_LCTL_AN_ENABLE;
	} else {
		/* Set PCS register for forced speed */

		/* Turn off bits for full duplex, speed, and autoneg */
		reg &= ~(E1000_PCS_LCTL_FSV_1000 |
		         E1000_PCS_LCTL_FSV_100 |
		         E1000_PCS_LCTL_FDV_FULL |
		         E1000_PCS_LCTL_AN_ENABLE);

		/* Check for duplex first */
		if (mac->forced_speed_duplex & E1000_ALL_FULL_DUPLEX)
			reg |= E1000_PCS_LCTL_FDV_FULL;

		/* Now set speed */
		if (mac->forced_speed_duplex & E1000_ALL_100_SPEED)
			reg |= E1000_PCS_LCTL_FSV_100;

		/* Force speed and force link */
		reg |= E1000_PCS_LCTL_FSD |
		       E1000_PCS_LCTL_FORCE_LINK |
		       E1000_PCS_LCTL_FLV_LINK_UP;

		DEBUGOUT1("Wrote 0x%08X to PCS_LCTL to configure forced link\n",
		          reg);
	}
	E1000_WRITE_REG(hw, E1000_PCS_LCTL, reg);

out:
	return E1000_SUCCESS;
}

/**
 *  e1000_sgmii_active_82575 - Return sgmii state
 *  @hw: pointer to the HW structure
 *
 *  82575 silicon has a serialized gigabit media independent interface (sgmii)
 *  which can be enabled for use in the embedded applications.  Simply
 *  return the current state of the sgmii interface.
 **/
static bool e1000_sgmii_active_82575(struct e1000_hw *hw)
{
	struct e1000_dev_spec_82575 *dev_spec;
	bool ret_val;

	DEBUGFUNC("e1000_sgmii_active_82575");

	if (hw->mac.type != e1000_82575 && hw->mac.type != e1000_82576) {
		ret_val = false;
		goto out;
	}

	dev_spec = (struct e1000_dev_spec_82575 *)hw->dev_spec;

	ret_val = dev_spec->sgmii_active;

out:
	return ret_val;
}

/**
 *  e1000_translate_register_82576 - Translate the proper register offset
 *  @reg: e1000 register to be read
 *
 *  Registers in 82576 are located in different offsets than other adapters
 *  even though they function in the same manner.  This function takes in
 *  the name of the register to read and returns the correct offset for
 *  82576 silicon.
 **/
u32 e1000_translate_register_82576(u32 reg)
{
	/*
	 * Some of the Kawela registers are located at different
	 * offsets than they are in older adapters.
	 * Despite the difference in location, the registers
	 * function in the same manner.
	 */
	switch (reg) {
	case E1000_TDBAL(0):
		reg = 0x0E000;
		break;
	case E1000_TDBAH(0):
		reg = 0x0E004;
		break;
	case E1000_TDLEN(0):
		reg = 0x0E008;
		break;
	case E1000_TDH(0):
		reg = 0x0E010;
		break;
	case E1000_TDT(0):
		reg = 0x0E018;
		break;
	case E1000_TXDCTL(0):
		reg = 0x0E028;
		break;
	case E1000_RDBAL(0):
		reg = 0x0C000;
		break;
	case E1000_RDBAH(0):
		reg = 0x0C004;
		break;
	case E1000_RDLEN(0):
		reg = 0x0C008;
		break;
	case E1000_RDH(0):
		reg = 0x0C010;
		break;
	case E1000_RDT(0):
		reg = 0x0C018;
		break;
	case E1000_RXDCTL(0):
		reg = 0x0C028;
		break;
	case E1000_SRRCTL(0):
		reg = 0x0C00C;
		break;
	default:
		break;
	}

	return reg;
}

/**
 *  e1000_reset_init_script_82575 - Inits HW defaults after reset
 *  @hw: pointer to the HW structure
 *
 *  Inits recommended HW defaults after a reset when there is no EEPROM
 *  detected. This is only for the 82575.
 **/
static s32 e1000_reset_init_script_82575(struct e1000_hw* hw)
{
	DEBUGFUNC("e1000_reset_init_script_82575");

	if (hw->mac.type == e1000_82575) {
		DEBUGOUT("Running reset init script for 82575\n");
		/* SerDes configuration via SERDESCTRL */
		e1000_write_8bit_ctrl_reg_generic(hw, E1000_SCTL, 0x00, 0x0C);
		e1000_write_8bit_ctrl_reg_generic(hw, E1000_SCTL, 0x01, 0x78);
		e1000_write_8bit_ctrl_reg_generic(hw, E1000_SCTL, 0x1B, 0x23);
		e1000_write_8bit_ctrl_reg_generic(hw, E1000_SCTL, 0x23, 0x15);

		/* CCM configuration via CCMCTL register */
		e1000_write_8bit_ctrl_reg_generic(hw, E1000_CCMCTL, 0x14, 0x00);
		e1000_write_8bit_ctrl_reg_generic(hw, E1000_CCMCTL, 0x10, 0x00);

		/* PCIe lanes configuration */
		e1000_write_8bit_ctrl_reg_generic(hw, E1000_GIOCTL, 0x00, 0xEC);
		e1000_write_8bit_ctrl_reg_generic(hw, E1000_GIOCTL, 0x61, 0xDF);
		e1000_write_8bit_ctrl_reg_generic(hw, E1000_GIOCTL, 0x34, 0x05);
		e1000_write_8bit_ctrl_reg_generic(hw, E1000_GIOCTL, 0x2F, 0x81);

		/* PCIe PLL Configuration */
		e1000_write_8bit_ctrl_reg_generic(hw, E1000_SCCTL, 0x02, 0x47);
		e1000_write_8bit_ctrl_reg_generic(hw, E1000_SCCTL, 0x14, 0x00);
		e1000_write_8bit_ctrl_reg_generic(hw, E1000_SCCTL, 0x10, 0x00);
	}

	return E1000_SUCCESS;
}

/**
 *  e1000_read_mac_addr_82575 - Read device MAC address
 *  @hw: pointer to the HW structure
 **/
static s32 e1000_read_mac_addr_82575(struct e1000_hw *hw)
{
	s32 ret_val = E1000_SUCCESS;

	DEBUGFUNC("e1000_read_mac_addr_82575");
	if (e1000_check_alt_mac_addr_generic(hw))
		ret_val = e1000_read_mac_addr_generic(hw);

	return ret_val;
}

/**
 * e1000_power_down_phy_copper_82575 - Remove link during PHY power down
 * @hw: pointer to the HW structure
 *
 * In the case of a PHY power down to save power, or to turn off link during a
 * driver unload, or wake on lan is not enabled, remove the link.
 **/
static void e1000_power_down_phy_copper_82575(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	struct e1000_mac_info *mac = &hw->mac;

	if (!(phy->ops.check_reset_block))
		return;

	/* If the management interface is not enabled, then power down */
	if (!(mac->ops.check_mng_mode(hw) || phy->ops.check_reset_block(hw)))
		e1000_power_down_phy_copper(hw);

	return;
}

/**
 *  e1000_remove_device_82575 - Free device specific structure
 *  @hw: pointer to the HW structure
 *
 *  If a device specific structure was allocated, this function will
 *  free it after shutting down the serdes interface if available.
 **/
void e1000_remove_device_82575(struct e1000_hw *hw)
{
	u16 eeprom_data = 0;

	/*
	 * If APM is enabled in the EEPROM then leave the port on for fiber
	 * serdes adapters.
	 */
	if (hw->bus.func == 0)
		hw->nvm.ops.read(hw, NVM_INIT_CONTROL3_PORT_A, 1, &eeprom_data);

	if (!(eeprom_data & E1000_NVM_APME_82575))
		e1000_shutdown_fiber_serdes_link_82575(hw);

	e1000_remove_device_generic(hw);
}

/**
 *  e1000_clear_hw_cntrs_82575 - Clear device specific hardware counters
 *  @hw: pointer to the HW structure
 *
 *  Clears the hardware counters by reading the counter registers.
 **/
static void e1000_clear_hw_cntrs_82575(struct e1000_hw *hw)
{
	volatile u32 temp;

	DEBUGFUNC("e1000_clear_hw_cntrs_82575");

	e1000_clear_hw_cntrs_base_generic(hw);

	temp = E1000_READ_REG(hw, E1000_PRC64);
	temp = E1000_READ_REG(hw, E1000_PRC127);
	temp = E1000_READ_REG(hw, E1000_PRC255);
	temp = E1000_READ_REG(hw, E1000_PRC511);
	temp = E1000_READ_REG(hw, E1000_PRC1023);
	temp = E1000_READ_REG(hw, E1000_PRC1522);
	temp = E1000_READ_REG(hw, E1000_PTC64);
	temp = E1000_READ_REG(hw, E1000_PTC127);
	temp = E1000_READ_REG(hw, E1000_PTC255);
	temp = E1000_READ_REG(hw, E1000_PTC511);
	temp = E1000_READ_REG(hw, E1000_PTC1023);
	temp = E1000_READ_REG(hw, E1000_PTC1522);

	temp = E1000_READ_REG(hw, E1000_ALGNERRC);
	temp = E1000_READ_REG(hw, E1000_RXERRC);
	temp = E1000_READ_REG(hw, E1000_TNCRS);
	temp = E1000_READ_REG(hw, E1000_CEXTERR);
	temp = E1000_READ_REG(hw, E1000_TSCTC);
	temp = E1000_READ_REG(hw, E1000_TSCTFC);

	temp = E1000_READ_REG(hw, E1000_MGTPRC);
	temp = E1000_READ_REG(hw, E1000_MGTPDC);
	temp = E1000_READ_REG(hw, E1000_MGTPTC);

	temp = E1000_READ_REG(hw, E1000_IAC);
	temp = E1000_READ_REG(hw, E1000_ICRXOC);

	temp = E1000_READ_REG(hw, E1000_ICRXPTC);
	temp = E1000_READ_REG(hw, E1000_ICRXATC);
	temp = E1000_READ_REG(hw, E1000_ICTXPTC);
	temp = E1000_READ_REG(hw, E1000_ICTXATC);
	temp = E1000_READ_REG(hw, E1000_ICTXQEC);
	temp = E1000_READ_REG(hw, E1000_ICTXQMTC);
	temp = E1000_READ_REG(hw, E1000_ICRXDMTC);

	temp = E1000_READ_REG(hw, E1000_CBTMPC);
	temp = E1000_READ_REG(hw, E1000_HTDPMC);
	temp = E1000_READ_REG(hw, E1000_CBRMPC);
	temp = E1000_READ_REG(hw, E1000_RPTHC);
	temp = E1000_READ_REG(hw, E1000_HGPTC);
	temp = E1000_READ_REG(hw, E1000_HTCBDPC);
	temp = E1000_READ_REG(hw, E1000_HGORCL);
	temp = E1000_READ_REG(hw, E1000_HGORCH);
	temp = E1000_READ_REG(hw, E1000_HGOTCL);
	temp = E1000_READ_REG(hw, E1000_HGOTCH);
	temp = E1000_READ_REG(hw, E1000_LENERRS);

	/* This register should not be read in copper configurations */
	if (hw->phy.media_type == e1000_media_type_internal_serdes)
		temp = E1000_READ_REG(hw, E1000_SCVPC);
}
