### sync cells Constraints

set sync_stage0_d_pins [get_pins -hier -filter {NAME =~ "*reg_q_reg[0]/D"}]

set_max_delay -through $sync_stage0_d_pins 20.000
set_false_path -hold -through $sync_stage0_d_pins