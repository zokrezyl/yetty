Refactorings:

* file names should be using with hyphen and small letters: for instance plugin-manager for the class PluginManager
* private members should use prefix "_" (underscore) instead of postfix
* member variables should be with underscore also in structs
* most methods should use Result<void> even if they return void! Almost all methods will have error condition!

