all:
	@echo nothing to be done for all:

clean:
	find . -name '*~' -delete
	$(RM) -rf doc/html/

doxygen:
	doxygen doc/Doxyfile

doc:	doxygen
	if [ `uname` = Darwin ] ; then open doc/html/index.html ; else $(BROWSER) doc/html/index.html ; fi
