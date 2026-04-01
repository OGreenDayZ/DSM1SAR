Pas d'inclusion de type : "../../dsm.h" ou "../pager/pager.h"

Ecrire plutôt : 
#include "dsm.h"
#include "pager/pager.h"

Pour corriger le rouge, mettre dans .vscode, un ```c_cpp_properties.json``` avec :
```
{
    "configurations": [
        {
            "name": "DSM1SAR-Config",
            "includePath": [
                "${workspaceFolder}/include",
                "${workspaceFolder}/src"
            ]
        }
    ],
    "version": 4
}
```

Le Makefile gèrera les inclusions avec le flag -I dans CFLAGS
