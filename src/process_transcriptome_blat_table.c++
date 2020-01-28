// Copyright 2019 Nadia Davidson for Peter Mac
// This program is distributed under the GNU
// General Public License. We also ask that you cite this software in
// publications where you made use of it for any part of the data
// analysis.

/** 
 ** Rewritten to c++ from the original R version.
 ** This code is pretty ugly, but we tried to keep variable names / methods as close
 ** to original R version for the purposes of transparence and to avoid introducing
 ** unforseen bugs.
 ** 
 ** Parses a .psl style alignment table and report inital candidate fusions
 **
 **
 ** Author: Nadia Davidson
 ** Modified: December 2019
 **/ 

#include <iostream>
#include <fstream>
#include <istream>
#include <string>
#include <sstream>
#include <vector>
#include <map>
#include <algorithm>
#include <regex>
#include <stdlib.h>
#include <algorithm>    

using namespace std;

// the help information which is printed when a user puts in the wrong
// combination of command line options.
void print_usage(){
  cerr << endl;
  cerr << "Usage: process_transcriptome_blat_table <blat table> <gap size> <ref_table> > <out table>" << endl;
  cerr << endl;
}

static const int MAX_OVERLAP=15 ;

class Position {
public:
  string chrom ;
  int start;
  int end;
  string gene;
  Position(){} ;
  Position(string c, int s, int e, string g) : chrom(c), start(s), end(e), gene(g) {} ;
};

class Alignment {
public:
  int score ;
  string t_id;
  int t_length;
  string q_id;
  int start;
  int end;
  int t_start;
  int t_end;
  string strand;

  Alignment( int sc, string id_t, int length_t, int start_q, int end_q, string id_q, int start_t, int end_t, string strnd ) :
    start(start_q), end(end_q), score(sc), t_id(id_t), t_length(length_t), q_id(id_q), t_start(start_t), t_end(end_t), strand(strnd) {};
};


template <typename T> vector<T> remove_redundant( vector<T> unreduced ){
  for(int a=0; a < unreduced.size(); ++a){
    for(int b=0; b < unreduced.size(); ++b){
      if(a==b) continue;
      if(unreduced[a].start<=unreduced[b].start && unreduced[a].end>=unreduced[b].end){
	unreduced.erase(unreduced.begin()+b);
	return(remove_redundant(unreduced));
      }
    }
  }
  return(unreduced);
}

template <typename T> vector<T> reduce(vector<T> unreduced){
  vector<int> index_to_keep;
  //sort vector
  sort(unreduced.begin(), unreduced.end(),[](T const& lhs, T const& rhs) { return lhs.start < rhs.start; }); 
  //loop through and find overlaps
  index_to_keep.push_back(0);
  int current=0;
  for(int i=1; i<unreduced.size(); ++i){
    int left_end=unreduced.at(current).end;
    int right_end=unreduced.at(i).end;
    int right_start=unreduced.at(i).start;
    if( left_end>=right_start ){
      if( left_end<right_end ) unreduced.at(current).end=right_end;
    } else {
      current=i;
      index_to_keep.push_back(i);
    }
  }
  //now copy to final vector
  vector<T> result;
  for(int i=0; i<index_to_keep.size(); ++i){
    result.push_back(unreduced.at(i));
  }

  return result;
}

template <typename T> void print_vec(vector<T> vec){
  for(int i=0; i < vec.size(); i++)
    cout << vec[i].start << "-" << vec[i].end << endl;
  cout << "****" << endl;
}


void multi_gene(vector<Alignment> this_al, const map<string, Position> & gene_positions, int gap_size){

  //if it only matches one transcript, return
  if(this_al.size()<2) return;
  
  //if one reference transcript covered the whole range then return.
  int min=this_al[0].start;
  int max=this_al[0].end;
  for(int a=1; a < this_al.size() ; a++){ //loop to get the min and max
    if(this_al[a].start<min) min=this_al[a].start;
    if(this_al[a].end>max) max=this_al[a].end;
  }
  //loop again to see if a single alignments coverages it all
  for(int a=0; a < this_al.size() ; a++){
    if(this_al[a].start==min && this_al[a].end==max) return;
  }

  //now get just the non-redundant set of transcript alignments
  vector<Alignment> regions=remove_redundant(this_al);

  //what do these alignment correspond to in the genome?
  vector< string > gene_names; //actually these are the transcript IDs
  map< string, string > gene_name_lookup; //maps trans ids to gene symbols 
  smatch m; //extract the gene id
  for(int a=0; a<regions.size(); a++){
    regex_search(regions[a].q_id,m,regex("_(EN[^_]*)__"));
    gene_names.push_back(m[1].str());
    gene_name_lookup[regions[a].q_id]=gene_positions.at(m[1].str()).gene;
  }

  //split by chrom
  map< string, vector<int> > split_chroms;
  vector<Position> gp; 
  for(int i=0 ; i < gene_names.size() ; ++i){
    const string trans=gene_names.at(i);
    if(gene_positions.find(trans) == gene_positions.end()){ //throw an error if transcript id not found...
      cerr << "Could not find transcript ID: " << trans << " in reference table. Ignoring chimera." << endl;
      return;
    } 
    string gp_chrom=gene_positions.at(trans).chrom; 
    if(gp_chrom=="chrM") return; //likely false chimera from library prep or other artificat if fusion involved chrM
    split_chroms[gp_chrom].push_back(i);
    gp.push_back(gene_positions.at(trans));
  }
  
  vector<Alignment> new_ranges;
  map< string, vector<int> >::iterator sc_itr = split_chroms.begin();
  for(;sc_itr!=split_chroms.end(); ++sc_itr){
    vector<int> sc=sc_itr->second;
    if(sc.size()==1){
      new_ranges.push_back(regions[sc[0]]);
    } else {
      //add extra bases to the end of each gene to check if they are close togther/same gene?
      vector<Position> ir;
      for(int s=0; s<sc.size(); ++s){
	Position new_pos(gp[sc[s]].chrom,gp[sc[s]].start,gp[sc[s]].end+gap_size,"");
	ir.push_back(new_pos);
      }
      vector<Position> iru = reduce(ir);
    //merge alignments if the genes in the genome are close together
      //loop over reduced regions. Collect transcripts that overlaps and merge alignment
      for(int r=0 ; r < iru.size() ; ++r){
	vector<Alignment> regions_w;
	for(int s=0; s < ir.size() ; ++s){
	  if(ir[s].start>=iru[r].start && ir[s].end<=iru[r].end)
	    regions_w.push_back(regions[sc[s]]);
	} //add to merged set of alignment ranges
	vector<Alignment> reduced_region_w=reduce(regions_w);
	new_ranges.insert( new_ranges.end(), reduced_region_w.begin(), reduced_region_w.end() );
      }
    }
  }
  sort(new_ranges.begin(), new_ranges.end(),[](Alignment const& lhs,Alignment const& rhs) { return lhs.start < rhs.start; });

  // find overlaps. continue until we find one the meets the criteria (small or no overlap)
  const int OVERLAP_BUFFER=15; //maximum number of bases that both genes can share
  for(int i=0; i< new_ranges.size()-1; ++i){
    if(abs(new_ranges[i].end-new_ranges[i+1].start)<OVERLAP_BUFFER ){
      // check they aren't from the same gene
      string gene1=gene_name_lookup[new_ranges[i].q_id];
      string gene2=gene_name_lookup[new_ranges[i+1].q_id];
      if(gene1!=gene2){ //can't be back splicing within the same gene
	cout << new_ranges[i].t_id << "\t" //print candidate
	   << std::min(new_ranges[i].end,new_ranges[i+1].start)-1 << "\t" 
	   << std::max(new_ranges[i].end,new_ranges[i+1].start)+1 << "\t"
	   << gene1 << ":" << gene2 << "\t"
	     << new_ranges[i].t_length << "\t" 
	     << new_ranges[i].q_id << "\t" << new_ranges[i].t_start << "\t" << new_ranges[i].t_end << "\t"
	     << new_ranges[i].strand << "\t"
	     << new_ranges[i+1].q_id << "\t" << new_ranges[i+1].t_start << "\t" << new_ranges[i+1].t_end << "\t"
	     << new_ranges[i+1].strand << "\t"
	     << endl;
	
      }
    }
  }

}


// the real stuff starts here.
int main(int argc, char **argv){

  if(argc!=4){
    print_usage();
    exit(1);
  }

  //minimum gap size in genome is argv[2]
  int gap_size=atoi(argv[2]);

  //get the gene names and positions
  /** 
   ** Now read in the gene to transcript ID mapping 
   **/
  ifstream file; 
  file.open(argv[3]);
  if(!(file.good())){
    cerr << "Unable to open file " << argv[3] << endl;
    exit(1);
  }
  //assume the first line is the header
  //  map<string,string > symbols;
  map<string,Position > gene_positions;
  string line;
  getline (file,line);
  int n_name=0; int n_name2=0; int n_chrom=0; int n_start=0; int n_end=0; 
  istringstream line_stream(line);
  for(int i=0; line_stream ; i++){
    string temp;
    line_stream >> temp;
    if(temp=="name") n_name=i;
    if(temp=="name2") n_name2=i;
    if(temp=="chrom") n_chrom=i;
    if(temp=="txStart") n_start=i;
    if(temp=="txEnd") n_end=i;
  }
  while ( getline (file,line) ){
    istringstream line_stream(line);
    string gene; string trans; string chrom;
    int start; int end;
    for(int i=0; line_stream ; i++){
      string temp;
      line_stream >> temp;
      if(i==n_name) trans=temp; 
      if(i==n_name2) gene=temp; 
      if(i==n_chrom) chrom=temp; 
      if(i==n_start) start=atoi(temp.c_str()); 
      if(i==n_end) end=atoi(temp.c_str()); 
    }
    Position new_pos (chrom, start, end, gene);
    gene_positions[trans]=new_pos;
  }
  file.close();
  cerr << "Done reading in transcript IDs" << endl;

  //Now load the .psl file
  string filename=argv[1];
  file.open(filename);
  if(!(file.good())){
    cerr << "Unable to open file " << filename << endl;
    exit(1);
  } 
  /**********  Read all the blat alignments ****************/
  cerr << "Reading the input alignment file, "<< filename << endl;
  map<string,vector<Alignment> > split_results;
  for(int i=0; i<5 ; i++) getline(file,line) ; //skip the first 5 lines
  while(getline(file,line) ){
    istringstream line_stream(line);
    vector<string> columns;
    string temp;
    while(getline(line_stream, temp,'\t'))
      columns.push_back(temp);
    if(columns.size()<7){ 
      cerr << columns.size() ; 
      cerr << "Warning: unexpected number of columns in psl file, "
	   << filename << endl; continue ; 
    }
    Alignment al(atoi(columns[9].c_str()),
		 columns[0],
		 atoi(columns[1].c_str()),
		 atoi(columns[2].c_str()),
		 atoi(columns[3].c_str()),
		 columns[5],
		 atoi(columns[7].c_str()),
		 atoi(columns[8].c_str()),
		 columns[4]
		 );
    
    split_results[columns[0]].push_back(al);
  }
  file.close();

  //now loop over all the alignments to find candidate fusions
  map< string , vector<Alignment> >:: iterator itr =  split_results.begin();
  int i=0;
  for(; itr!=split_results.end(); itr++){
    multi_gene(itr->second, gene_positions, gap_size);
    if(i%10000 == 0  ) cerr << i << endl;
    i++;
  }
  cerr << i << " alignments processed. Finished." << endl;

  return(0);
}
