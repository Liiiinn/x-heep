# Nexys-A7-100T implementation strategy settings.
# This script is sourced during Vivado project creation (before launch_runs),
# so set_property calls on impl_1 take effect for the subsequent run.

set impl_run [get_runs -quiet impl_1]

if {[llength $impl_run] > 0} {
  # Timing-oriented strategy: runs place, route, and two phys_opt passes.
  set_property strategy Performance_ExplorePostRoutePhysOpt $impl_run

  # Push placement harder to reduce route-dominated paths.
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
# TIMING-14 (LUT on clock tree) - 3 known violations (same as CW305):
#   JTAG TCK inverter LUT (architecturally necessary on Artix-7) and
#   SPI slave SCK Vivado-auto-inserted BUFGMUX control LUTs (harmless;
#   SPI clock domain declared async via set_clock_groups).
if {[llength [get_methodology_checks -quiet TIMING-14]] > 0} {
  set_property SEVERITY {Warning} [get_methodology_checks TIMING-14]
}
