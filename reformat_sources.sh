#!/bin/bash

for FILE in `find . | grep -E "\.h$|\.cpp$"`; do
	astyle --options=cloudmus.astylerc --suffix=none $FILE
done
