#!/usr/bin/env bash
/nix/store/1f8n0gml7sqa5dzyiwjrkghrab9qzpp6-python3-3.12.10-env/bin/python ./plot_bandwidth.py \
	exclusive.interleaved.2.20K.0.0008.0.9.1.False.56.2.csv \
	colocation_node.interleaved.2.20K.0.0008.0.9.1.False.28.2.csv \
	colocation_socket.interleaved.2.20K.0.0008.0.9.1.False.28.2.csv \
	coexecution_node.interleaved.2.20K.0.0008.0.9.1.False.56.2.csv \
	coexecution_socket.interleaved.2.20K.0.0008.0.9.1.False.56.2.csv \
	schedcoop_node.interleaved.2.20K.0.0008.0.9.2.False.56.2.csv \
	schedcoop_socket.interleaved.2.20K.0.0008.0.9.2.False.56.2.csv \
	-l "exclusive,colocation_node,colocation_socket,coexecution_node,coexecution_socket,schedcoop_node,schedcoop_socket" \
	--color 'turquoise,#000080,#4169E1,#006400,#32CD32,#D2691E,#FF8C00' \
	--type total  \
	--no-legend  --no-title \
	--legend-ncol 7 --export-legend "legend.pdf" \
	--zoom "150,170" --zoom-loc "0.625,0.08,0.33,0.45" \
	--linestyles "solid,solid,dashed,solid,dashed,solid,dashed" \
	--fontsize 20 --figsize "10,5" \
	-o combined.pdf

#--legend-loc "0.19,0.15" \
