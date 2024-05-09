#!/bin/sh
# cleanup.sh
rm -rf x x.* *.x *.tmp tmp.* *.tmp[0-9] ~* .DS_Store .vs *.log *.recipe *.sdf *.opensdf *.suo

rm -rf x64 */x64 Debug */Debug

rm -rf */*.vcxproj.user

# Fixes VS error "System.Collections.IStructuralEquatable is not a GenericTypeDefinition. MakeGenericType may only be called on a type for which Type.IsGenericTypeDefinition is true."
rm -rf ~/sford/AppData/Local/Microsoft/VisualStudio/11.0/ComponentModelCache
