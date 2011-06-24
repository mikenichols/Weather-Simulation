#! /bin/bash
# I don't believe the width and height options actually work
mencoder mf://*.png -mf w=400:h=300:fps=80:type=png -ovc copy -oac copy -o output.avi
