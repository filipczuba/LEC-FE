#include "driver.hpp"
#include "parser.hpp"

// Generazione di un'istanza per ciascuna della classi LLVMContext,
// Module e IRBuilder. Nel caso di singolo modulo è sufficiente
LLVMContext *context = new LLVMContext;
Module *module = new Module("Kaleidoscope", *context);
IRBuilder<> *builder = new IRBuilder(*context);

Value *LogErrorV(const std::string Str) {
  std::cerr << Str << std::endl;
  return nullptr;
}

/* Il codice seguente sulle prime non è semplice da comprendere.
   Esso definisce una utility (funzione C++) con due parametri:
   1) la rappresentazione di una funzione llvm IR, e
   2) il nome per un registro SSA
   La chiamata di questa utility restituisce un'istruzione IR che alloca un double
   in memoria e ne memorizza il puntatore in un registro SSA cui viene attribuito
   il nome passato come secondo parametro. L'istruzione verrà scritta all'inizio
   dell'entry block della funzione passata come primo parametro.
   Si ricordi che le istruzioni sono generate da un builder. Per non
   interferire con il builder globale, la generazione viene dunque effettuata
   con un builder temporaneo TmpB
*/
static AllocaInst *CreateEntryBlockAlloca(Function *fun, StringRef VarName,Type* T = Type::getDoubleTy(*context)) {
  IRBuilder<> TmpB(&fun->getEntryBlock(), fun->getEntryBlock().begin());
  return TmpB.CreateAlloca(T, nullptr, VarName);

}

// Implementazione del costruttore della classe driver
driver::driver(): trace_parsing(false), trace_scanning(false) {};

// Implementazione del metodo parse
int driver::parse (const std::string &f) {
  file = f;                    // File con il programma
  location.initialize(&file);  // Inizializzazione dell'oggetto location
  scan_begin();                // Inizio scanning (ovvero apertura del file programma)
  yy::parser parser(*this);    // Istanziazione del parser
  parser.set_debug_level(trace_parsing); // Livello di debug del parsed
  int res = parser.parse();    // Chiamata dell'entry point del parser
  scan_end();                  // Fine scanning (ovvero chiusura del file programma)
  return res;
}

// Implementazione del metodo codegen, che è una "semplice" chiamata del 
// metodo omonimo presente nel nodo root (il puntatore root è stato scritto dal parser)
void driver::codegen() {
  root->codegen(*this);
};

/************************* Sequence tree **************************/
SeqAST::SeqAST(RootAST* first, RootAST* continuation):
  first(first), continuation(continuation) {};

// La generazione del codice per una sequenza è banale:
// mediante chiamate ricorsive viene generato il codice di first e 
// poi quello di continuation (con gli opportuni controlli di "esistenza")
Value *SeqAST::codegen(driver& drv) {
  if (first != nullptr) {
    Value *f = first->codegen(drv);
  } else {
    if (continuation == nullptr) return nullptr;
  }
  Value *c = continuation->codegen(drv);
  return nullptr;
};

/********************* Number Expression Tree *********************/
NumberExprAST::NumberExprAST(double Val): Val(Val) {};

lexval NumberExprAST::getLexVal() const {
  // Non utilizzata, Inserita per continuità con versione precedente
  lexval lval = Val;
  return lval;
};

// Non viene generata un'struzione; soltanto una costante LLVM IR
// corrispondente al valore float memorizzato nel nodo
// La costante verrà utilizzata in altra parte del processo di generazione
// Si noti che l'uso del contesto garantisce l'unicità della costanti 
Value *NumberExprAST::codegen(driver& drv) {  
  return ConstantFP::get(*context, APFloat(Val));
};

/******************** Variable Expression Tree ********************/
VariableExprAST::VariableExprAST(const std::string &Name, ExprAST* Index, bool IsArray): Name(Name), Index(Index), IsArray(IsArray) {};

lexval VariableExprAST::getLexVal() const {
  lexval lval = Name;
  return lval;
};

// NamedValues è una tabella che ad ogni variabile (che, in Kaleidoscope1.0, 
// può essere solo un parametro di funzione) associa non un valore bensì
// la rappresentazione di una funzione che alloca memoria e restituisce in un
// registro SSA il puntatore alla memoria allocata. Generare il codice corrispondente
// ad una varibile equivale dunque a recuperare il tipo della variabile 
// allocata e il nome del registro e generare una corrispondente istruzione di load
// Negli argomenti della CreateLoad ritroviamo quindi: (1) il tipo allocato, (2) il registro
// SSA in cui è stato messo il puntatore alla memoria allocata (si ricordi che A è
// l'istruzione ma è anche il registro, vista la corrispodenza 1-1 fra le due nozioni), (3)
// il nome del registro in cui verrà trasferito il valore dalla memoria
Value *VariableExprAST::codegen(driver& drv) {
  
  AllocaInst* A = drv.NamedValues[Name];

  if(!A) {
    //Se A non è non è in NamedValues provo a cercarla nelle variabili globali.
    GlobalVariable* G = module->getNamedGlobal(Name);

    //Se non c'è nemmeno nei globali lancio errore.
    if(!G) { return LogErrorV("Variabile "+Name+" not definita"); }

    if(IsArray) {
      Value *VIndex = Index->codegen(drv);

      if(!VIndex) {return nullptr;}

      Value *FPIndex = builder->CreateFPTrunc(VIndex, Type::getFloatTy(*context));
      Value *INTIndex = builder->CreateFPToSI(FPIndex, Type::getInt32Ty(*context));

      Value *GEP = builder->CreateInBoundsGEP(G->getValueType(),G, INTIndex);

      return builder->CreateLoad(Type::getDoubleTy(*context),GEP,Name.c_str());
    }
    else { 
      return builder->CreateLoad(G->getValueType(),G,Name.c_str());
    }

  }

  if(IsArray) {
      Value *VIndex = Index->codegen(drv);

      if(!VIndex) {return nullptr;}

      Value *FPIndex = builder->CreateFPTrunc(VIndex, Type::getFloatTy(*context));
      Value *INTIndex = builder->CreateFPToSI(FPIndex, Type::getInt32Ty(*context));

      Value *GEP = builder->CreateInBoundsGEP(A->getAllocatedType(),A, INTIndex);

      return builder->CreateLoad(Type::getDoubleTy(*context),GEP,Name.c_str());
  }
  else { 
    //Prende il valore della variabile Name da NamedValues
    return builder->CreateLoad(A->getAllocatedType(),A,Name.c_str());
  }
   
}

/******************** Binary Expression Tree **********************/
BinaryExprAST::BinaryExprAST(char Op, ExprAST* LHS, ExprAST* RHS):
  Op(Op), LHS(LHS), RHS(RHS) {};

// La generazione del codice in questo caso è di facile comprensione.
// Vengono ricorsivamente generati il codice per il primo e quello per il secondo
// operando. Con i valori memorizzati in altrettanti registri SSA si
// costruisce l'istruzione utilizzando l'opportuno operatore
Value *BinaryExprAST::codegen(driver& drv) {
  Value *L = LHS->codegen(drv);
  Value *R = RHS->codegen(drv);
  if (!L || !R) 
     return nullptr;
  switch (Op) {
  case '+':
    return builder->CreateFAdd(L,R,"addres");
  case '-':
    return builder->CreateFSub(L,R,"subres");
  case '*':
    return builder->CreateFMul(L,R,"mulres");
  case '/':
    return builder->CreateFDiv(L,R,"addres");
  case '<':
    return builder->CreateFCmpULT(L,R,"lttest");
  case '=':
    return builder->CreateFCmpUEQ(L,R,"eqtest");
  default:  
    std::cout << Op << std::endl;
    return LogErrorV("Operatore binario non supportato");
  }
};

/********************* Call Expression Tree ***********************/
/* Call Expression Tree */
CallExprAST::CallExprAST(std::string Callee, std::vector<ExprAST*> Args):
  Callee(Callee),  Args(std::move(Args)) {};

lexval CallExprAST::getLexVal() const {
  lexval lval = Callee;
  return lval;
};

Value* CallExprAST::codegen(driver& drv) {
  // La generazione del codice corrispondente ad una chiamata di funzione
  // inizia cercando nel modulo corrente (l'unico, nel nostro caso) una funzione
  // il cui nome coincide con il nome memorizzato nel nodo dell'AST
  // Se la funzione non viene trovata (e dunque non è stata precedentemente definita)
  // viene generato un errore
  Function *CalleeF = module->getFunction(Callee);
  if (!CalleeF)
     return LogErrorV("Funzione non definita");
  // Il secondo controllo è che la funzione recuperata abbia tanti parametri
  // quanti sono gi argomenti previsti nel nodo AST
  if (CalleeF->arg_size() != Args.size())
     return LogErrorV("Numero di argomenti non corretto");
  // Passato con successo anche il secondo controllo, viene predisposta
  // ricorsivamente la valutazione degli argomenti presenti nella chiamata 
  // (si ricordi che gli argomenti possono essere espressioni arbitarie)
  // I risultati delle valutazioni degli argomenti (registri SSA, come sempre)
  // vengono inseriti in un vettore, dove "se li aspetta" il metodo CreateCall
  // del builder, che viene chiamato subito dopo per la generazione dell'istruzione
  // IR di chiamata
  std::vector<Value *> ArgsV;
  for (auto arg : Args) {
     ArgsV.push_back(arg->codegen(drv));
     if (!ArgsV.back())
        return nullptr;
  }
  return builder->CreateCall(CalleeF, ArgsV, "calltmp");
}

/************************* If Expression Tree *************************/
IfExprAST::IfExprAST(ExprAST* Cond, ExprAST* TrueExp, ExprAST* FalseExp):
   Cond(Cond), TrueExp(TrueExp), FalseExp(FalseExp) {};
   
Value* IfExprAST::codegen(driver& drv) {
    // Viene dapprima generato il codice per valutare la condizione, che
    // memorizza il risultato (di tipo i1, dunque booleano) nel registro SSA 
    // che viene "memorizzato" in CondV. 
    Value* CondV = Cond->codegen(drv);
    if (!CondV)
       return nullptr;
    
    // Ora bisogna generare l'istruzione di salto condizionato, ma prima
    // vanno creati i corrispondenti basic block nella funzione attuale
    // (ovvero la funzione di cui fa parte il corrente blocco di inserimento)
    Function *function = builder->GetInsertBlock()->getParent();
    BasicBlock *TrueBB =  BasicBlock::Create(*context, "trueexp", function);
    // Il blocco TrueBB viene inserito nella funzione dopo il blocco corrente
    BasicBlock *FalseBB = BasicBlock::Create(*context, "falseexp");
    BasicBlock *MergeBB = BasicBlock::Create(*context, "endcond");
    // Gli altri due blocchi non vengono ancora inseriti perché le istruzioni
    // previste nel "ramo" true del condizionale potrebbe dare luogo alla creazione
    // di altri blocchi, che naturalmente andrebbero inseriti prima di FalseBB
    
    // Ora possiamo crere l'istruzione di salto condizionato
    builder->CreateCondBr(CondV, TrueBB, FalseBB);
    
    // "Posizioniamo" il builder all'inizio del blocco true, 
    // generiamo ricorsivamente il codice da eseguire in caso di
    // condizione vera e, in chiusura di blocco, generiamo il saldo 
    // incondizionato al blocco merge
    builder->SetInsertPoint(TrueBB);
    Value *TrueV = TrueExp->codegen(drv);
    if (!TrueV)
       return nullptr;
    builder->CreateBr(MergeBB);
    
    // Come già ricordato, la chiamata di codegen in TrueExp potrebbe aver inserito 
    // altri blocchi (nel caso in cui la parte trueexp sia a sua volta un condizionale).
    // Ne consegue che il blocco corrente potrebbe non coincidere più con TrueBB.
    // Il branch alla parte merge deve però essere effettuato dal blocco corrente,
    // che dunque va recuperato. Ed è fondamentale sapere da quale blocco origina
    // il salto perché tale informazione verrà utilizzata da un'istruzione PHI.
    // Nel caso in cui non sia stato inserito alcun nuovo blocco, la seguente
    // istruzione corrisponde ad una NO-OP
    TrueBB = builder->GetInsertBlock();
    function->insert(function->end(), FalseBB);
    
    // "Posizioniamo" il builder all'inizio del blocco false, 
    // generiamo ricorsivamente il codice da eseguire in caso di
    // condizione falsa e, in chiusura di blocco, generiamo il saldo 
    // incondizionato al blocco merge
    builder->SetInsertPoint(FalseBB);
    
    Value *FalseV = FalseExp->codegen(drv);
    if (!FalseV)
       return nullptr;
    builder->CreateBr(MergeBB);
    
    // Esattamente per la ragione spiegata sopra (ovvero il possibile inserimento
    // di nuovi blocchi da parte della chiamata di codegen in FalseExp), andiamo ora
    // a recuperare il blocco corrente 
    FalseBB = builder->GetInsertBlock();
    function->insert(function->end(), MergeBB);
    
    // Andiamo dunque a generare il codice per la parte dove i due "flussi"
    // di esecuzione si riuniscono. Impostiamo correttamente il builder
    builder->SetInsertPoint(MergeBB);
  
    // Il codice di riunione dei flussi è una "semplice" istruzione PHI: 
    //a seconda del blocco da cui arriva il flusso, TrueBB o FalseBB, il valore
    // del costrutto condizionale (si ricordi che si tratta di un "expression if")
    // deve essere copiato (in un nuovo registro SSA) da TrueV o da FalseV
    // La creazione di un'istruzione PHI avviene però in due passi, in quanto
    // il numero di "flussi entranti" non è fissato.
    // 1) Dapprima si crea il nodo PHI specificando quanti sono i possibili nodi sorgente
    // 2) Per ogni possibile nodo sorgente, viene poi inserita l'etichetta e il registro
    //    SSA da cui prelevare il valore 
    PHINode *PN = builder->CreatePHI(Type::getDoubleTy(*context), 2, "condval");
    PN->addIncoming(TrueV, TrueBB);
    PN->addIncoming(FalseV, FalseBB);
    return PN;
};

/********************** Block Expression Tree *********************/
BlockExprAST::BlockExprAST(std::vector<InitAST*> Def, std::vector<StatementAST*> Val): 
         Def(std::move(Def)), Val(std::move(Val)) {};

Value* BlockExprAST::codegen(driver& drv) {
   // Un blocco è un'espressione preceduta dalla definizione di una o più variabili locali.
   // Le definizioni sono opzionali e tuttavia necessarie perché l'uso di un blocco
   // abbia senso. Ad ogni variabile deve essere associato il valore di una costante o il valore di
   // un'espressione. Nell'espressione, arbitraria, possono chiaramente comparire simboli di
   // variabile. Al riguardo, la gestione dello scope (ovvero delle regole di visibilità)
   // è implementata nel modo seguente, in cui, come esempio, consideremo la definzione: var y = x+1
   // 1) Viene dapprima generato il codice per valutare l'espressione x+1.
   //    L'area di memoria da cui "prelevare" il valore di x è scritta in un
   //    registro SSA che è parte della (rappresentazione interna della) istruzione alloca usata
   //    per allocare la memoria corrispondente e che è registrata nella symbol table
   //    Per i parametri della funzione, l'istruzione di allocazione viene generata (come già sappiamo)
   //    dalla chiamata di codegen in FunctionAST. Per le variabili locali viene generata nel presente
   //    contesto. Si noti, di passaggio, che tutte le istruzioni di allocazione verranno poi emesse
   //    nell'entry block, in ordine cronologico rovesciato (rispetto alla generazione). Questo perché
   //    la routine di utilità (CreateEntryBlockAlloca) genera sempre all'inizio del blocco.
   // 2) Ritornando all'esempio, bisogna ora gestire l'assegnamento ad y gestendone la visibilità. 
   //    Come prima cosa viene generata l'istruzione alloca per y. 
   //    Questa deve essere inserita nella symbol table per futuri riferimenti ad y
   //    all'interno del blocco. Tuttavia, se un'istruzione alloca per y fosse già presente nella symbol
   //    table (nel caso y sia un parametro) bisognerebbe "rimuoverla" temporaneamente e re-inserirla
   //    all'uscita del blocco. Questo è ciò che viene fatto dal presente codice, che utilizza
   //    al riguardo il vettore di appoggio "AllocaTmp" (che naturalmente è un vettore di
   //    di (puntatori ad) istruzioni di allocazione
   std::vector<AllocaInst*> AllocaTmp;
   for (int i=0, e=Def.size(); i<e; i++) {
      // Per ogni definizione di variabile si genera il corrispondente codice che
      // (in questo caso) non restituisce un registro SSA ma l'istruzione di allocazione
      AllocaInst *boundval = (AllocaInst*)Def[i]->codegen(drv);
      if (!boundval) 
         return nullptr;
      // Viene temporaneamente rimossa la precedente istruzione di allocazione
      // della stessa variabile (nome) e inserita quella corrente
      AllocaTmp.push_back(drv.NamedValues[Def[i]->getName()]);
      drv.NamedValues[Def[i]->getName()] = boundval;
   };
   // Ora (ed è la parte più "facile" da capire) viene generato il codice che
   // valuta l'espressione. Eventuali riferimenti a variabili vengono risolti
   // nella symbol table appena modificata
   Value *blockvalue;
   for (int i = 0, e=Val.size(); i<e; i++) {
       blockvalue = Val[i]->codegen(drv);
      if (!blockvalue)
         return nullptr;
   }
   
   // Prima di uscire dal blocco, si ripristina lo scope esterno al costrutto
   for (int i=0, e=Def.size(); i<e; i++) {
        drv.NamedValues[Def[i]->getName()] = AllocaTmp[i];
   };
   // Il valore del costrutto/espressione var è ovviamente il valore (il registro SSA)
   // restituito dal codice di valutazione dell'espressione
   return blockvalue;
};



/*INIT*/
std::string& InitAST::getName() {return Name;};
IT InitAST::getType() {return INIT;};


/************************* Var binding Tree *************************/
VarBindingAST::VarBindingAST(const std::string Name, ExprAST* Val): Name(Name), Val(Val),IsArray(false) {};
   
VarBindingAST::VarBindingAST(const std::string Name,double Size,std::vector<ExprAST*> Values): Name(Name), Size(Size), Values(std::move(Values)), IsArray(true) {};

std::string& VarBindingAST::getName() { return Name; };
IT VarBindingAST::getType() {return BINDING;};


AllocaInst* VarBindingAST::codegen(driver& drv) {

  Function *fun = builder->GetInsertBlock()->getParent();
  AllocaInst *Alloca = nullptr;
  if(IsArray) {
      ArrayType *AT = ArrayType::get(Type::getDoubleTy(*context),Size);
      AllocaInst* Alloca = CreateEntryBlockAlloca(fun,Name,AT);
      Value* actVal;
      for(int i=0; i<Size;i++){
        Value *ElemVal = (i < Values.size()) ? Values[i]->codegen(drv) : ConstantFP::get(*context, APFloat(0.0));
        if (!ElemVal)
            return nullptr;
        Value *ElemPtr = builder->CreateInBoundsGEP(AT, Alloca, ConstantInt::get(*context, APInt(32, i, true)), Name + "_idx_" + std::to_string(i));
        builder->CreateStore(ElemVal, ElemPtr);
      }
  }
  else {
      Value *BoundVal = Val->codegen(drv);
      if (!BoundVal) 
        return nullptr;

      Alloca = CreateEntryBlockAlloca(fun, Name);
    
      builder->CreateStore(BoundVal, Alloca);
  }
   

   
return Alloca;
};

/************************* Prototype Tree *************************/
PrototypeAST::PrototypeAST(std::string Name, std::vector<std::string> Args):
  Name(Name), Args(std::move(Args)), emitcode(true) {};  //Di regola il codice viene emesso

lexval PrototypeAST::getLexVal() const {
   lexval lval = Name;
   return lval;	
};

const std::vector<std::string>& PrototypeAST::getArgs() const { 
   return Args;
};

// Previene la doppia emissione del codice. Si veda il commento più avanti.
void PrototypeAST::noemit() { 
   emitcode = false; 
};

Function *PrototypeAST::codegen(driver& drv) {
  // Costruisce una struttura, qui chiamata FT, che rappresenta il "tipo" di una
  // funzione. Con ciò si intende a sua volta una coppia composta dal tipo
  // del risultato (valore di ritorno) e da un vettore che contiene il tipo di tutti
  // i parametri. Si ricordi, tuttavia, che nel nostro caso l'unico tipo è double.
  
  // Prima definiamo il vettore (qui chiamato Doubles) con il tipo degli argomenti
  std::vector<Type*> Doubles(Args.size(), Type::getDoubleTy(*context));
  // Quindi definiamo il tipo (FT) della funzione
  FunctionType *FT = FunctionType::get(Type::getDoubleTy(*context), Doubles, false);
  // Infine definiamo una funzione (al momento senza body) del tipo creato e con il nome
  // presente nel nodo AST. ExternalLinkage vuol dire che la funzione può avere
  // visibilità anche al di fuori del modulo
  Function *F = Function::Create(FT, Function::ExternalLinkage, Name, *module);

  // Ad ogni parametro della funzione F (che, è bene ricordare, è la rappresentazione 
  // llvm di una funzione, non è una funzione C++) attribuiamo ora il nome specificato dal
  // programmatore e presente nel nodo AST relativo al prototipo
  unsigned Idx = 0;
  for (auto &Arg : F->args())
    Arg.setName(Args[Idx++]);

  /* Abbiamo completato la creazione del codice del prototipo.
     Il codice può quindi essere emesso, ma solo se esso corrisponde
     ad una dichiarazione extern. Se invece il prototipo fa parte
     della definizione "completa" di una funzione (prototipo+body) allora
     l'emissione viene fatta al momendo dell'emissione della funzione.
     In caso contrario nel codice si avrebbe sia una dichiarazione
     (come nel caso di funzione esterna) sia una definizione della stessa
     funzione.
  */
  if (emitcode) {
    F->print(errs());
    fprintf(stderr, "\n");
  };
  
  return F;
}

/************************* Function Tree **************************/
FunctionAST::FunctionAST(PrototypeAST* Proto, ExprAST* Body): Proto(Proto), Body(Body) {};

Function *FunctionAST::codegen(driver& drv) {
  // Verifica che la funzione non sia già presente nel modulo, cioò che non
  // si tenti una "doppia definizion"
  Function *function = 
      module->getFunction(std::get<std::string>(Proto->getLexVal()));
  // Se la funzione non è già presente, si prova a definirla, innanzitutto
  // generando (ma non emettendo) il codice del prototipo
  if (!function)
    function = Proto->codegen(drv);
  else
    return nullptr;
  // Se, per qualche ragione, la definizione "fallisce" si restituisce nullptr
  if (!function)
    return nullptr;  

  // Altrimenti si crea un blocco di base in cui iniziare a inserire il codice
  BasicBlock *BB = BasicBlock::Create(*context, "entry", function);
  builder->SetInsertPoint(BB);
 
  // Ora viene la parte "più delicata". Per ogni parametro formale della
  // funzione, nella symbol table si registra una coppia in cui la chiave
  // è il nome del parametro mentre il valore è un'istruzione alloca, generata
  // invocando l'utility CreateEntryBlockAlloca già commentata.
  // Vale comunque la pena ricordare: l'istruzione di allocazione riserva 
  // spazio in memoria (nel nostro caso per un double) e scrive l'indirizzo
  // in un registro SSA
  // Il builder crea poi un'istruzione che memorizza il valore del parametro x
  // (al momento contenuto nel registro SSA %x) nell'area di memoria allocata.
  // Si noti che il builder conosce il registro che contiene il puntatore all'area
  // perché esso è parte della rappresentazione C++ dell'istruzione di allocazione
  // (variabile Alloca) 
  
  for (auto &Arg : function->args()) {
    // Genera l'istruzione di allocazione per il parametro corrente
    AllocaInst *Alloca = CreateEntryBlockAlloca(function, Arg.getName());
    // Genera un'istruzione per la memorizzazione del parametro nell'area
    // di memoria allocata
    builder->CreateStore(&Arg, Alloca);
    // Registra gli argomenti nella symbol table per eventuale riferimento futuro
    drv.NamedValues[std::string(Arg.getName())] = Alloca;
  } 
  
  // Ora può essere generato il codice corssipondente al body (che potrà
  // fare riferimento alla symbol table)
  if (Value *RetVal = Body->codegen(drv)) {
    // Se la generazione termina senza errori, ciò che rimane da fare è
    // di generare l'istruzione return, che ("a tempo di esecuzione") prenderà
    // il valore lasciato nel registro RetVal 
    builder->CreateRet(RetVal);

    // Effettua la validazione del codice e un controllo di consistenza
    verifyFunction(*function);
 
    // Emissione del codice su su stderr) 
    function->print(errs());
    fprintf(stderr, "\n");
    return function;
  }

  // Errore nella definizione. La funzione viene rimossa
  function->eraseFromParent();
  return nullptr;
};

/*****************************+*********+*/


GlobalAST::GlobalAST(std::string Name,bool IsArray, double Size): Name(Name), IsArray(IsArray), Size(Size) {};

Value* GlobalAST::codegen(driver &drv) {

  GlobalVariable *gVar;
  if(!IsArray) {
    gVar = new GlobalVariable(*module, Type::getDoubleTy(*context), false, GlobalValue::CommonLinkage, ConstantFP::get(*context, APFloat(0.0)) , Name);
  }
  else if(IsArray && (Size > 0)) {
    ArrayType* AT = ArrayType::get(Type::getDoubleTy(*context),Size);
    gVar = new GlobalVariable(*module, AT, false, GlobalValue::CommonLinkage,ConstantFP::getNullValue(AT), Name);
  }
  else {
    return LogErrorV("Tentativo di inizializzare array con dimensione < 0.");
  }

  gVar->print(errs());
  fprintf(stderr, "\n");
  return gVar;
};


AssignmentExprAST::AssignmentExprAST(std::string Name, ExprAST* Val, ExprAST* Index, bool IsArray): Name(Name), Val(Val), Index(Index), IsArray(IsArray) {};

std::string& AssignmentExprAST::getName(){ return Name; };

IT AssignmentExprAST::getType() {return ASSIGNMENT;};

Value* AssignmentExprAST::codegen(driver& drv) {

  Value* V = Val->codegen(drv);
  if(!V) { return LogErrorV("Valore non adatto all'assegnamento.");}

  AllocaInst* A = drv.NamedValues[Name];

  if(!A) {
    //Se A non è non è in NamedValues provo a cercarla nelle variabili globali.
    GlobalVariable* G = module->getNamedGlobal(Name);

    //Se non c'è nemmeno nei globali lancio errore.
    if(!G) {  return LogErrorV("Variabile non dichiarata: " + Name); }

    if(IsArray) {
      Value *VIndex = Index->codegen(drv);

      if(!VIndex) {return nullptr;}

      Value *FPIndex = builder->CreateFPTrunc(VIndex, Type::getFloatTy(*context));
      Value *INTIndex = builder->CreateFPToSI(FPIndex, Type::getInt32Ty(*context));

      Value *GEP = builder->CreateInBoundsGEP(G->getValueType(),G, INTIndex);

      builder->CreateStore(V,GEP);
    }
    else { 
      builder->CreateStore(V,G);
    }
    return V;
  }
  //Se la variabile è locale
  else {
    //Se è un array
    if(IsArray) {
        Value *VIndex = Index->codegen(drv);

        if(!VIndex) {return nullptr;}

        Value *FPIndex = builder->CreateFPTrunc(VIndex, Type::getFloatTy(*context));
        Value *INTIndex = builder->CreateFPToSI(FPIndex, Type::getInt32Ty(*context));

        Value *GEP = builder->CreateInBoundsGEP(A->getAllocatedType(),A, INTIndex);

        builder->CreateStore(V,GEP);
    }
    else { 
      //Prende il valore della variabile Name da NamedValues
      builder->CreateStore(V,A);
    }
    return V;

  }
}

ForStmtAST::ForStmtAST(InitAST* Init, ExprAST* CondExp, AssignmentExprAST* Assignment, StatementAST* Statement): Init(Init), CondExp(CondExp), Assignment(Assignment), Statement(Statement) {};

Value* ForStmtAST::codegen(driver& drv) {


  //Genera i BB nella funzione attuale, inserisco subito quello responsabile per l'inizalizzazione;
  Function *function = builder->GetInsertBlock()->getParent();
  BasicBlock *EntryBB =  BasicBlock::Create(*context, "for_entry",function);
  BasicBlock *CondBB = BasicBlock::Create(*context,"for_condition",function);
  BasicBlock *LoopBB = BasicBlock::Create(*context, "for_body",function);
  BasicBlock *ExitBB = BasicBlock::Create(*context, "for_exit",function);



  //Creo il branch incodizionato tra function e EntryBB
  builder->CreateBr(EntryBB);
  builder -> SetInsertPoint(EntryBB);

  //Gestione delle variabili in Init
  AllocaInst* AllocaTmp = nullptr;
  Value* IVal = Init->codegen(drv);
  if(!IVal) {return nullptr;}

  if(Init->getType() == BINDING) {

      AllocaTmp = drv.NamedValues[Init->getName()];
      drv.NamedValues[Init->getName()] = (AllocaInst*) IVal;
  }
  

  //Branch incondizionato tra Entry e Condition, cambio InsertPoint del builder.
  builder->CreateBr(CondBB);
  builder->SetInsertPoint(CondBB);

  //Viene generata l'istruzione di branch condizionato.
  Value* condVal = CondExp -> codegen(drv);
  if(!condVal) return nullptr;
  builder -> CreateCondBr(condVal,LoopBB,ExitBB);


  //Inserisco LoopBB e cambio InsertPoint
  builder->SetInsertPoint(LoopBB);

  //Genera il body
  Value* loopBody = Statement->codegen(drv);
  if(!loopBody) return nullptr;

  Value* stepAssignment = Assignment->codegen(drv);
  if(!stepAssignment) return nullptr;

  //Branch incodizionato a fine body per il ritorno a Condition
  builder->CreateBr(CondBB);

  //Inserimento di ExitBB e cambio InsertPoint

  builder->SetInsertPoint(ExitBB);

  //FOR restituisce 0.0 come da tutorial di LLVM
  PHINode *P = builder->CreatePHI(Type::getDoubleTy(*context),1,"for_phi");
  P->addIncoming(ConstantFP::getNullValue(Type::getDoubleTy(*context)),CondBB);

  //Restore della variabile in Init
  if(Init->getType() == BINDING) {
    drv.NamedValues[Init->getName()] = AllocaTmp;
  }

  
  return P;
};


IfStmtAST::IfStmtAST(ExprAST* Cond, StatementAST* TrueExp, StatementAST* FalseExp):
   Cond(Cond), TrueExp(TrueExp), FalseExp(FalseExp) {};
   
Value* IfStmtAST::codegen(driver& drv) {
    // Viene dapprima generato il codice per valutare la condizione, che
    // memorizza il risultato (di tipo i1, dunque booleano) nel registro SSA 
    // che viene "memorizzato" in CondV. 
    Value* CondV = Cond->codegen(drv);
    if (!CondV)
       return nullptr;
    
    // Ora bisogna generare l'istruzione di salto condizionato, ma prima
    // vanno creati i corrispondenti basic block nella funzione attuale
    // (ovvero la funzione di cui fa parte il corrente blocco di inserimento)
    Function *function = builder->GetInsertBlock()->getParent();
    BasicBlock *TrueBB =  BasicBlock::Create(*context, "trueexp", function);
    // Il blocco TrueBB viene inserito nella funzione dopo il blocco corrente
    BasicBlock *FalseBB = BasicBlock::Create(*context, "falseexp");
    BasicBlock *MergeBB = BasicBlock::Create(*context, "endcond");
    // Gli altri due blocchi non vengono ancora inseriti perché le istruzioni
    // previste nel "ramo" true del condizionale potrebbe dare luogo alla creazione
    // di altri blocchi, che naturalmente andrebbero inseriti prima di FalseBB
    
    // Ora possiamo crere l'istruzione di salto condizionato
    builder->CreateCondBr(CondV, TrueBB, FalseBB);
    
    // "Posizioniamo" il builder all'inizio del blocco true, 
    // generiamo ricorsivamente il codice da eseguire in caso di
    // condizione vera e, in chiusura di blocco, generiamo il saldo 
    // incondizionato al blocco merge
    builder->SetInsertPoint(TrueBB);
    Value *TrueV = TrueExp->codegen(drv);
    if (!TrueV)
       return nullptr;
    builder->CreateBr(MergeBB);
    
    // Come già ricordato, la chiamata di codegen in TrueExp potrebbe aver inserito 
    // altri blocchi (nel caso in cui la parte trueexp sia a sua volta un condizionale).
    // Ne consegue che il blocco corrente potrebbe non coincidere più con TrueBB.
    // Il branch alla parte merge deve però essere effettuato dal blocco corrente,
    // che dunque va recuperato. Ed è fondamentale sapere da quale blocco origina
    // il salto perché tale informazione verrà utilizzata da un'istruzione PHI.
    // Nel caso in cui non sia stato inserito alcun nuovo blocco, la seguente
    // istruzione corrisponde ad una NO-OP
    TrueBB = builder->GetInsertBlock();
    function->insert(function->end(), FalseBB);
    
    // "Posizioniamo" il builder all'inizio del blocco false, 
    // generiamo ricorsivamente il codice da eseguire in caso di
    // condizione falsa e, in chiusura di blocco, generiamo il saldo 
    // incondizionato al blocco merge
    builder->SetInsertPoint(FalseBB);
    
    
    if (FalseExp){
      Value *FalseV = FalseExp->codegen(drv);
      if(!FalseV) {
        return nullptr;
      }
      FalseBB = builder->GetInsertBlock();
    }
       
    builder->CreateBr(MergeBB);
    
    // Esattamente per la ragione spiegata sopra (ovvero il possibile inserimento
    // di nuovi blocchi da parte della chiamata di codegen in FalseExp), andiamo ora
    // a recuperare il blocco corrente 
    FalseBB = builder->GetInsertBlock();
    function->insert(function->end(), MergeBB);
    
    // Andiamo dunque a generare il codice per la parte dove i due "flussi"
    // di esecuzione si riuniscono. Impostiamo correttamente il builder
    builder->SetInsertPoint(MergeBB);
  
    // Il codice di riunione dei flussi è una "semplice" istruzione PHI: 
    //a seconda del blocco da cui arriva il flusso, TrueBB o FalseBB, il valore
    // del costrutto condizionale (si ricordi che si tratta di un "expression if")
    // deve essere copiato (in un nuovo registro SSA) da TrueV o da FalseV
    // La creazione di un'istruzione PHI avviene però in due passi, in quanto
    // il numero di "flussi entranti" non è fissato.
    // 1) Dapprima si crea il nodo PHI specificando quanti sono i possibili nodi sorgente
    // 2) Per ogni possibile nodo sorgente, viene poi inserita l'etichetta e il registro
    //    SSA da cui prelevare il valore 
    PHINode *PN = builder->CreatePHI(Type::getDoubleTy(*context), 2, "condval");
    PN->addIncoming(ConstantFP::getNullValue(Type::getDoubleTy(*context)), TrueBB);
    PN->addIncoming(ConstantFP::getNullValue(Type::getDoubleTy(*context)), FalseBB);
    return PN;
};


BooleanExprAST::BooleanExprAST(char Op, ExprAST* LHS, ExprAST* RHS): Op(Op), LHS(LHS), RHS(RHS) {};

Value* BooleanExprAST::codegen(driver& drv) {
  Value *L = LHS->codegen(drv);
  Value *R = RHS ? RHS->codegen(drv) : nullptr;

  if(!L) return nullptr;

  switch(Op){
    case 'A':
      return builder->CreateAnd(L,R,"andres");
    case 'O':
      return builder->CreateOr(L,R,"orres");
    case 'N':
      return builder->CreateNot(L,"notres");
    default:
      std::cout << Op << std::endl;
      return LogErrorV("Operatore booleano non supportato");
  }
};