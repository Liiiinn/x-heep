# CW305 implementation strategy settings.
# This script is sourced during Vivado project creation (before launch_runs),
# so set_property calls on impl_1 take effect for the subsequent run.

set impl_run [get_runs -quiet impl_1]

if {[llength $impl_run] > 0} {
  # Timing-oriented strategy: runs place, route, and two phys_opt passes.
  set_property strategy Performance_ExplorePostRoutePhysOpt $impl_run

  # Push placement harder to reduce route-dominated paths (key issue on ftg256 package).
  set_property STEPS.PLACE_DESIGN.ARGS.DIRECTIVE ExtraNetDelay_high $impl_run

  # Pre-route physical optimisation.
  set_property STEPS.PHYS_OPT_DESIGN.IS_ENABLED true $impl_run
  set_property STEPS.PHYS_OPT_DESIGN.ARGS.DIRECTIVE AggressiveExplore $impl_run

  # Aggressive routing exploration.
  set_property STEPS.ROUTE_DESIGN.ARGS.DIRECTIVE AggressiveExplore $impl_run

  # Post-route physical optimisation (retiming, hold fixing, etc.).
  set_property STEPS.POST_ROUTE_PHYS_OPT_DESIGN.IS_ENABLED true $impl_run
  set_property STEPS.POST_ROUTE_PHYS_OPT_DESIGN.ARGS.DIRECTIVE AggressiveExplore $impl_run
}

# ---------------------------------------------------------------------------
# Methodology check severity overrides
# ---------------------------------------------------------------------------
# TIMING-14 (LUT on clock tree) - 3 known violations:
#   #1  JTAG TCK path: tc_clk_inverter generates "tck_ni = ~tck_i" via a LUT1.
#       On Artix-7 there is no dedicated primitive to invert a clock without a
#       LUT; MMCM 180-degree phase is the only alternative and is impractical
#       for a low-frequency JTAG clock (< 20 MHz). The violation is harmless.
#   #2/3 SPI slave SCK path: Vivado auto-inserts a BUFGMUX inside the pad cell
#       to route spi_slave_sck_io to the global clock network.  The BUFGMUX
#       select/input LUTs are Vivado-internal and cannot be removed through RTL
#       alone.  The SPI clock domain is fully declared asynchronous to the system
#       clock via set_clock_groups, so no CDC risk exists.
# Downgrade from Critical Warning -> Warning to keep the build log clean.
if {[llength [get_methodology_checks -quiet TIMING-14]] > 0} {
  set_property SEVERITY {Warning} [get_methodology_checks TIMING-14]
}
