all:
	@echo done

.PHONY:	doc
doc:
	doxygen doc/Doxyfile
	[ `uname` = Darwin ] && open doc/html/index.html

clean:
	find . -name '*~' -delete
	$(RM) -r doc/html/
