# Run with: gnuplot plot_efficiency.gnuplot
# Produces: efficiency_S_vs_a.png  and  efficiency_S_vs_a_noerror.png

set datafile separator ','
set terminal pngcairo size 900,600 enhanced font 'Arial,12'
set grid
set key top right

# ── Plot 1: S vs a, one curve per BER (BER=0, 0.0001, 0.001 only — 0.005/0.01
#    have FER≈1 so theoretical≈0 and are less informative) ────────────────────
set output 'efficiency_S_vs_a.png'
set title 'Stop-and-Wait Efficiency  S  vs propagation factor  a'
set xlabel 'a = T_{prop} / T_{frame}'
set ylabel 'Efficiency S'
set yrange [0:1.1]
set xrange [-0.02:1.1]

# CSV columns: BER(1), prop_delay_us(2), a(3), FER(4), S_measured(5), S_theoretical(6)
plot \
  'efficiency_results.csv' using ($1==0.0    ? $3 : 1/0):5 with linespoints lw 2 pt 7 title 'meas  BER=0', \
  'efficiency_results.csv' using ($1==0.0    ? $3 : 1/0):6 with lines      lw 1 dt 2  title 'theo  BER=0', \
  'efficiency_results.csv' using ($1==0.0001 ? $3 : 1/0):5 with linespoints lw 2 pt 5 title 'meas  BER=0.0001', \
  'efficiency_results.csv' using ($1==0.0001 ? $3 : 1/0):6 with lines      lw 1 dt 2  title 'theo  BER=0.0001', \
  'efficiency_results.csv' using ($1==0.001  ? $3 : 1/0):5 with linespoints lw 2 pt 9 title 'meas  BER=0.001', \
  'efficiency_results.csv' using ($1==0.001  ? $3 : 1/0):6 with lines      lw 1 dt 2  title 'theo  BER=0.001'

# ── Plot 2: BER=0 only — clearest comparison of measured vs 1/(1+2a) ─────────
set output 'efficiency_S_vs_a_noerror.png'
set title 'Stop-and-Wait Efficiency  S  vs  a  (BER = 0)'

plot \
  'efficiency_results.csv' using ($1==0.0 ? $3 : 1/0):5 with linespoints lw 2 pt 7 lc rgb 'blue' title 'measured', \
  'efficiency_results.csv' using ($1==0.0 ? $3 : 1/0):6 with lines      lw 2 dt 2  lc rgb 'red'  title 'theoretical  1/(1+2a)'
