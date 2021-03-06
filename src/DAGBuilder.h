#ifndef DAG_BUILDER_H
#define DAG_BUILDER_H

#include <fstream>
#include <sstream>

// #include "DGBuilder.h"
#include "DAGNode.h"
#include "FunctionInfoPass.h"


class DAGBuilder 
{
private:
	// DGBuilder *DG_Builder; //dependence graph builder
	DAGVertexList Vertices;
	DAGValueList VertexByValue;
	bool hasBeenInitialized;
	bool DAGIsLocked;


public:
	DAGBuilder() : hasBeenInitialized(false){}
	~DAGBuilder() {}

	DAGVertexList retrieveDAG() 
	{
		assert( DAGIsLocked && "DAGBuilder has not been locked! Do so before retrieving the DAG.");
		return Vertices;
	}

	void lock() { DAGIsLocked = true; }
	// DGBuilder* getDGBuilder() 
	// { 
	// 	assert( DG_Builder != nullptr && "DGBuilder has not been set. Use createDG()");
	// 	return DG_Builder;
	// }

	void init()
	{
		Vertices.clear();
		DAGIsLocked = false;
		hasBeenInitialized = true;
	}

	/// DAG
	bool add(llvm::Value* value)
	{
		assert( hasBeenInitialized && "Builder has not been initialized! Do so with init()");
		
		if ( llvm::isa<llvm::Instruction>(value) )
		{
			/// Ignore the following
			auto inst = llvm::cast<llvm::Instruction>(value);
			if ( isInvalidOperator(inst) )
				return false;
			if ( isBranch(inst) )
				return false;
			if ( isReturn(inst) )
				return false;

			addInstruction(value);
		}

		return true;
	}

	bool addVoidFunction(llvm::Value* CallValue, int varWidth, 
						 int varDepth, int opWidth, int opDepth)
	{
		
		auto callInst = llvm::cast<llvm::CallInst>(CallValue);
		DAGNode *funcNode = addVertex(callInst, FUNC);
		auto function = callInst->getCalledFunction();
		if (function)
			funcNode->setConstName( function->getName() );
		else
			funcNode->setConstName("Indirect function call");

		//update node dimensions
		funcNode->setVarWidth(varWidth);
		funcNode->setVarDepth(varDepth);
		funcNode->setOpWidth(opWidth);
		funcNode->setOpDepth(opDepth);

		return true;
	}


private:
	
	bool addInstruction(llvm::Value* value, DAGNode* parentNode=nullptr)
	{    
		auto inst = llvm::cast<llvm::Instruction>(value);

		// remove unneeded instructions
		if ( llvm::isa<llvm::AllocaInst>(inst) )
			return false;
		if ( llvm::isa<llvm::CmpInst>(inst) )
			return false;

		DAGNode *valNode=nullptr, 
				*instNode=nullptr/*,
				*funcNode=nullptr*/;		


		/// Store
		if ( llvm::isa<llvm::StoreInst>(inst) ) 
		{
			addStoreInst(valNode, instNode, inst);
		}
		/// Load (load values only have one operand, the load value)
		else if ( llvm::isa<llvm::LoadInst>(inst) )
		{
			addLoadInst(valNode, instNode, inst);
		}
		//THIS ISN'T WORKING, EVEN THOUGH IT'S BEING CALLED IN THE EXACT
		// SAME WAY AS IN FUNCTIONINFOPASS.CPP
		// /// Function call
		// else if ( llvm::isa<llvm::CallInst>(inst) ) 
		// {

		// 	valNode = addVertex(inst, VAL);
		// 	// valNode->setVarWidth( analysis.varWidth );
		// 	// valNode

		// 	// FOR LATER...
		// 	// if function is void, just set inst node
		// 	// 		auto funcType = ->getFunctionType();
		// 	//		funcType->dump();
		// 	// else set value and inst node

		// }
		/// Other
		else
		{
			checkAndSet(valNode, instNode, inst);

			// recursively add operands
			// inst->dump();
			addOperand(inst->getOperand(0), instNode); // left operand
			addOperand(inst->getOperand(1), instNode); // right operand
		}

		// add children to parent
		if ( parentNode != nullptr )
		{
			addEdge(parentNode, valNode);
		}
		
		return true;
	}

	void addStoreInst(DAGNode* &valNode, DAGNode* &instNode, llvm::Instruction *storeInst)
	{
		//i.e. store %type %storeVal, %pointerType %targetVal

		llvm::Value *storeVal, *targetVal;
		storeVal = storeInst->getOperand(0);
		targetVal = storeInst->getOperand(1);
		
		// add target variable value node
		if ( isValuePresent(targetVal) )
		{
			auto key = VertexByValue[targetVal];
			valNode = Vertices[key];
		}
		else
		{
			valNode = addVertex(targetVal, VAL);
		}

		// set name
		std::string storeValName, targetValName, storeInstName;
		if ( storeVal->hasName() )
		{
			storeValName = storeVal->getName();				
		}
		else
		{
			storeValName = getConstName(storeVal);
		}
		if ( targetVal->hasName() )
		{
			targetValName = targetVal->getName();				
		}
		// else
		// {
		// 	targetValName = getConstName(targetVal);
		// }
		storeInstName = "store(" + storeValName + " to " + targetValName + ")";


		// store instructions will always have a unique ID
		instNode = addVertex(storeInst, INST);
		instNode->setConstName(storeInstName);
		instNode->setVarWidth(1);
		instNode->setVarDepth(1);

		// add edges
		addEdge(valNode, instNode);

		// recursively add the store value
		addOperand(storeVal, instNode);
	}

	void addLoadInst(DAGNode* &valNode, DAGNode* &instNode, llvm::Instruction *loadInst)
	{
		llvm::Value *loadVal;
		DAGNode *loadValNode = nullptr;
		loadVal = loadInst->getOperand(0);

		// CheckAndSet is not necessary as each load inst WILL
		// have a unique address.
		// 
		checkAndSet(valNode, instNode, loadInst);
		// valNode = addVertex(loadInst, VAL);
		// instNode = addVertex(loadInst, INST);
		// instNode->setValueNode(valNode);

		// The load value node already exists if it is
		// being loaded.
		auto key = VertexByValue[loadVal];
		loadValNode = Vertices[key];

		// get name
		std::string loadValName = loadVal->getName();
		loadValName = "load_" + loadValName;

		// set names
		valNode->setConstName("val_" + loadValName);
		instNode->setConstName("inst_" + loadValName);
		instNode->setVarWidth(1);
		instNode->setVarDepth(1);

		//Add edges
		// edge from valNode->instNode added in checkAndSet
		// addEdge(valNode, instNode); 	//i.e. load_a->load
		addEdge(instNode, loadValNode);	//i.e. load->a

		// If a value is being loaded it is assumed that
		// the value has already been stored, so the
		// recursive addOperand call is NOT necessary.
		//
		// addOperand(loadVal, instNode);
	}

	void checkAndSet(DAGNode* &valNode, DAGNode* &instNode, llvm::Value *value)
	{
		if ( isValuePresent(value) )
		{
			auto key = VertexByValue[value];
			instNode = Vertices[key];
			valNode = instNode->getValueNode();
		}
		else
		{
			// create a value and inst nodes for the new llvm::value
			valNode = addVertex(value, VAL);
			instNode = addVertex(value, INST);
			instNode->setValueNode(valNode);

			// if the llvm::value is a binary operator inst then update
			// the add operator width and depth
			if ( llvm::isa<llvm::BinaryOperator>(value) )
			{
				instNode->setOpWidth(1);
				instNode->setOpDepth(1);
			}

			// add an edge valNode->instNode
			addEdge(valNode, instNode);
		}
	}

	void addOperand(llvm::Value* operand, DAGNode* parentNode)
	{
		DAGNode* valNode;

		if ( llvm::isa<llvm::Instruction>(operand) )
		{
			addInstruction(operand, parentNode);
		}
		else
		{
			if ( isValuePresent(operand) )
			{	
				auto key = VertexByValue[operand];
				valNode = Vertices[key];
			}
			else
			{
				std::string constValName;
				constValName = getConstName(operand);
				valNode = addVertex(operand, VAL);
				valNode->setConstName(constValName);
			}
			addEdge(parentNode, valNode);
		}
	}

	std::string getConstName(llvm::Value* constVal)
	{
		std::string constValName;
		if ( llvm::isa<llvm::ConstantInt>(constVal) )
		{
			auto CI = llvm::cast<llvm::ConstantInt>(constVal);
			if ( CI->getBitWidth() <= 64 )
			{
				int constInt = CI->getSExtValue();
				constValName = std::to_string(constInt);
			}
		}
		if ( llvm::isa<llvm::ConstantFP>(constVal) )
		{
			// add stuff...
		}

		return constValName;
	}

	DAGNode* addVertex(llvm::Value* value, vertex_t type)
	{
		auto vertex = new DAGNode(value, type);
		Vertices[vertex] = vertex;
		VertexByValue[value] = vertex;

		return vertex;
	}

	void addEdge(DAGNode* currentNode, DAGNode* nextNode)
	{
		currentNode->addSuccessor(nextNode);
		nextNode->addPredecessor(currentNode);
	}
	
	bool isValuePresent(llvm::Value* key)
	{
		if( VertexByValue.count(key) == 0 )
			return false;
		else
			return true;
	}

	bool isInvalidOperator(llvm::Instruction *inst)
	{
		const char* opName = inst->getOpcodeName(inst->op_begin()->get()->getValueID());
		if (strcmp(opName, "<Invalid operator>") == 0)
			return true;
		else
			return false;
	}

	bool isBranch(llvm::Instruction *inst)
	{
    	if (inst->getOpcode() == llvm::Instruction::Br)
			return true;
		else
			return false;
	}

	bool isAlloca(llvm::Instruction *inst)
	{
    	if (inst->getOpcode() == llvm::Instruction::Alloca)
			return true;
		else
			return false;
	}

	bool isReturn(llvm::Instruction *inst)
	{
    	if (inst->getOpcode() == llvm::Instruction::Ret)
			return true;
		else
			return false;
	}

	/// Traversal
	bool findSourceNodes(std::vector<DAGNode*> &sourceNodes)
	{
		if ( Vertices.empty() )
			return false;

		sourceNodes.clear();
		for (auto vertex_pair : Vertices)
		{
			if ( !vertex_pair.second->hasPredecessors() )
				sourceNodes.push_back(vertex_pair.second);
		}
		return true;
	}

	int findHeight()
	{
		std::vector<DAGNode*> sourceNodes;
		if ( !findSourceNodes(sourceNodes) )
		{
			llvm::outs() << " ERROR: No source nodes were found!\n";
			return false;
		}
		if ( sourceNodes.empty() )
			return 0;

		std::vector<int> sourceDepths(sourceNodes.size()); //vector to record depths
		int index = 0;

		for ( auto sourceNode : sourceNodes )
		{
			//recursive helper funciton that is fed each source/root node
			// sourceDepths[index] = 0;
			// std::cout << "\nSource node[" << sourceNode->getName() << "]:" << std::endl;
			sourceNode->setAllUnvisited();
			sourceDepths[index] = findHeightWrapper(sourceNode);
			index++;
		}

		//find maxDepth
		// int i = 0;
		int maxDepth = 1;
		for ( auto depth : sourceDepths )
		{
			// i++;
			// std::cout << "depth[" << i << "] = " << depth << std::endl;
			if ( depth > maxDepth )
				maxDepth = depth;
		}

		return maxDepth;
	}

	int findHeightWrapper(DAGNode *node)
	{
		node->setVisited(true);
		if ( !node->hasSuccessors() )
			return 1;

		// std::cout << "getting successors for Node[" << node->getName() << "]..." << std::endl;
		auto Successors = node->getSuccessors();
		std::vector<int> depths(Successors.size());
		int index = 0;

		for ( auto successor_pair : Successors )
		{
			auto successor = successor_pair.second;
			if ( !successor->hasBeenVisited() )
			{

				//find total varDepth
				if ( successor->isStoreInstNext() )
				{
					depths[index] = findHeightWrapper(successor) + 1;
				}
				else
				{
					depths[index] = findHeightWrapper(successor);
				}
				// std::cout << "Node[" << successor->getName() << "] depth = " << depths[index] << std::endl;
				index++;
			}
		}

		return *std::max_element( depths.begin(), depths.end() );
	}





	// int getMaxVarWidth(DAGNode *root)
	// {

	// }

public:
	// void collectAdjNodes()
	// {
	// 	assert( DAGIsLocked && "DAG has not been locked! Do so with lock()");
	// 	for (int i = 0; i < ID; ++i)
	// 	{
	// 		DAGVertices[i]->addAdjNodes( DAGVertices[i]->getLeft() );
	// 		DAGVertices[i]->addAdjNodes( DAGVertices[i]->getRight() );
	// 	}
	// }

	// void createDG()
	// {
	// 	assert( DAGIsLocked && "DAG has not been locked! Do so with lock()");
	// 	DG_Builder = new DGBuilder(DAGVertices, ID);
		
	// 	DG_Builder->createDG();
	// 	DG_Builder->createVDG();
	// 	// DG_Builder->createODG();
	// }

	// void fini()
	// {
	// 	collectAdjNodes();
	// 	createDG();
	// 	// createVDG();
	// }



	void print()
	{
		assert( DAGIsLocked && "DAG has not been locked! Do so with lock()");
		llvm::outs() << "\nDAG Nodes:\n";
		DAGNode *vertex;
		int i = 0;
		for (auto vertex_pair : Vertices)
		{
			vertex = vertex_pair.second;
			llvm::outs() << "-------------------------------------------------------------\n";
			llvm::outs() << "Node" << i << ":\n";
			vertex->print();
			if ( vertex->hasSuccessors() )
			{
				llvm::outs() << "           Next Nodes: ";
				vertex->printSuccessorsNames();
				llvm::outs() << "       Previous Nodes: ";
				vertex->printPredecessorsNames();
			}

			// llvm::outs() << "\nPretty Print...\n";
			// vertex->prettyPrint();
			// 
			llvm::outs() << "-------------------------------------------------------------\n";
			i++;
		}

		/// Print source/root nodes [test] [passed]
		///***************************************
		// auto sourceNodes = findSourceNodes();
		// llvm::outs() << "\nSource Nodes (" << sourceNodes.size() << "):\n";
		// for (auto sourceNode : sourceNodes)
		// {
		// 	sourceNode->print();
		// 	llvm::outs() << "\n\n";
		// }
		///***************************************

		// llvm::outs() << "\nTotal height = " << findHeight() << "\n\n";

    	llvm::outs() << "\n";
	}

	// void printDependencyGraph()
	// {
	// 	assert( DAGIsLocked && "DAG has not been locked! Do so with lock()");
	// 	DG_Builder->printDG();
	// }
	

	bool DOTGenerateFile(std::string functionName)
	{
		std::vector<DAGNode*> sourceNodes;
		if ( !findSourceNodes(sourceNodes) )
		{
			llvm::outs() << " ERROR: No source nodes were found!\n";
			return false;
		}

		// stream to collect file output
		std::ostringstream outSS;

		// init graph and node specs
		outSS << "digraph g{" << std::endl;
		outSS << "\tnode [shape = record,height = .1];" << std::endl << std::endl;
		outSS << "\tlabelloc = \"t\";" << std::endl;
		outSS << "\tlabel = \"" << functionName << "_DAG" << "\";" << std::endl;

		// generate node markup for each source/root node
		for (auto root : sourceNodes)
		{
			outSS << std::endl;
			outSS << "\tsubgraph " << root->getName() << " {" << std::endl;
			root->DOTcreateNode(outSS);
			DOTAddSuccessor(outSS, root);
			outSS << "\t}" << std::endl;
		}
		outSS << "}" << std::endl;

		// Create and add to the .dot file
		std::string filePath = "../../dotFiles/" + functionName + "_Graph.dot";
		std::ofstream dotFile(filePath);
		if ( dotFile.is_open() )
		{
			dotFile << outSS.str();
			dotFile.close();
		}
		else
		{
			llvm::outs() << " ERROR: DOT file did not open!\n";
			return false;
		}

		llvm::outs() << " -- DOT file generated at: " << filePath << "\n";
		return true;
	}

	bool DOTAddSuccessor(std::ostringstream &outSS, DAGNode* parentNode)
	{
		if ( !parentNode->hasSuccessors() )
			return false;

		auto Successors = parentNode->getSuccessors();
		DAGNode *successor;
		for (auto successor_pair : Successors)
		{
			successor = successor_pair.second;
			// to prevent an infinite loop mark as visited
			if ( !successor->hasBeenVisited() )
			{
				successor->setVisited(true);
				successor->DOTcreateNode(outSS, parentNode);
				DOTAddSuccessor(outSS, successor);
			}
		}
		parentNode->setAllUnvisited(); //reset visited status for next print call

		return true;
	}

};







#endif /* DAG_BUILDER_H */
