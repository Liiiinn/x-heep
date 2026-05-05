### JTAG Constraints

set_max_delay -through [get_nets -filter {NAME=~"*async*"} -of_objects [get_cells -hier -filter {REF_NAME =~ cdc_2phase_src* || ORIG_REF_NAME =~ cdc_2phase_src*}]] 20.000
set_false_path -hold -through [get_nets -filter {NAME=~"*async*"} -of_objects [get_cells -hier -filter {REF_NAME =~ cdc_2phase_src* || ORIG_REF_NAME =~ cdc_2phase_src*}]]

# Hold and max delay on 4 phases

set cdc4phase_data_src_q_nets [get_nets -hier -filter {NAME =~ "*data_src_q*"}]
set cdc4phase_req_src_q_nets  [get_nets -hier -filter {NAME =~ "*req_src_q*"}]

set_max_delay -through $cdc4phase_data_src_q_nets 20.000
set_false_path -hold -through $cdc4phase_data_src_q_nets

set_max_delay -through $cdc4phase_req_src_q_nets 20.000
set_false_path -hold -through $cdc4phase_req_src_q_nets