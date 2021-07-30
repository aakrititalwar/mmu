#include <iostream>
#include <fstream>
#include <string>
#include <getopt.h>
#include <cstring>
#include <cctype>
#include <string>
#include <bits/stdc++.h>
#include <queue>
using namespace std;

const int MAX_VPAGES = 64;
int MAX_FRAMES = 128;
int hand;
long int last_reset_instr = 0;
vector<int> randvals;
int ofs = 0;
long unsigned int Context_Switch = 0;
long unsigned int process_exits = 0; 
long long unsigned int cost = 0;
int rlinenum;
FILE* inpfile;
FILE* rfile;
static char linebuf[1024];
static char rbuf[1024];

long int instr_count_num = -1;
char curr_operation = '\0';
int vpage = -1;
const char* DELIM = " \t\n\r\v\f";

struct VMA{
    int start_vpage;
    int end_vpage;
    int write_protected;
    int file_mapped;
};

struct frame_t{
    int fid;
    int curr_pid;
    int curr_ass_vpage;
    unsigned int age_counter:32;
    int time_of_last_use = 0;

};

struct pte_t{
    unsigned int present:1;
    unsigned int refrenced:1;
    unsigned int pagedout:1;
    unsigned int modified:1;
    unsigned int frame_num:7;
    unsigned int write_protected:1;
    unsigned int file_mapped:1;
    unsigned int padding : 19;
};

struct Process{
    int pid;
    vector<VMA*> vma_list;
    pte_t page_table[MAX_VPAGES];
    long unsigned int UNMAP = 0;
    long unsigned int MAP = 0;
    long unsigned int IN = 0;
    long unsigned int FIN = 0;
    long unsigned int OUT = 0;
    long unsigned int FOUT = 0;
    long unsigned int SEGV = 0;
    long unsigned int SEGPROT = 0;
    long unsigned int ZERO = 0;
};

queue<struct frame_t*> framefreelist;
frame_t frame_table[128];
vector<Process*> pro_vector;
Process* CURRENT_PROCESS = NULL;

int myrandom(int burst) {
    //cout << "hi " << endl;
    //cout << "ofs" <<ofs << endl;
    //cout << "RLN" <<rlinenum << endl;
     if(ofs >= rlinenum){
         ofs = rlinenum - ofs;
     }
    //cout << "in random function " << ofs;
    //cout << "value" << randvals.at(ofs) << endl;
    int random = (randvals.at(ofs) % burst); 
    ofs = ofs + 1;
    return random;
    }

void reset_refrenced_bits(){
    for(int i = 0; i<MAX_FRAMES; i++){
        Process* curr_pro = pro_vector.at(frame_table[i].curr_pid);
        pte_t* pte = &(curr_pro->page_table[frame_table[i].curr_ass_vpage]);
        if(pte->present){
            pte->refrenced = 0;
        }

    }
}






class Pager {
   public:
    virtual frame_t* select_victim_frame() = 0;
};

class FIFO : public Pager{
    
    public:
    FIFO(){
       int hand = 0;
    }
    frame_t* select_victim_frame(){
        //cout << " in fifo svf" << endl;
        frame_t* victim_frame = &frame_table[hand];
        hand = hand + 1;
        if(hand >= MAX_FRAMES){
            hand =  0;
        }
        return victim_frame;

    }
    
};

class CLOCK : public Pager{
    public:
    CLOCK(){
        int hand = 0;
    }
    frame_t* select_victim_frame(){
        
        while(true){
            if(hand >= MAX_FRAMES){
                hand = 0;
            }
            frame_t* frame = &frame_table[hand];
            Process* curr_pro = pro_vector.at(frame->curr_pid);
            pte_t* pte = &(curr_pro->page_table[frame->curr_ass_vpage]);
            hand = hand + 1;
            if(pte->refrenced){
                pte->refrenced = 0;
                continue;
        }
        else{
            return frame;
        }
        }
    }

};

class RANDOM : public Pager
{
public:
    frame_t *select_victim_frame()
    {
        int rand = myrandom(MAX_FRAMES);
        //cout << "rand" << rand << endl;
        frame_t *victim = &frame_table[rand];
        return victim;
    }
};

class ESC : public Pager
{
    public:
    ESC(){
        int hand = 0;
    }
    int threshold = 49;
    frame_t* select_victim_frame(){
        int lowest_class  = 10;
        frame_t* victim_frame = NULL;
        int curr_hand_position = hand;
        //cout << "curr_hand_position" << curr_hand_position << endl;
        do{
        //cout << "hand" << hand;
        frame_t* frame = &frame_table[hand];
        pte_t* pte = &((pro_vector.at(frame->curr_pid))->page_table[frame->curr_ass_vpage]); 
        int curr_class = 2 * pte->refrenced + pte->modified;
        if(curr_class < lowest_class){
            lowest_class = curr_class;
            victim_frame = frame;
        }
        hand = hand + 1;
        if(hand>= MAX_FRAMES)
        hand = 0;
        if(lowest_class == 0)
        break;
        }
        while(hand != curr_hand_position);
        if (instr_count_num >= this->threshold)
        {
            this->threshold = instr_count_num + 50;
            reset_refrenced_bits();
        }
        hand = victim_frame->fid + 1;
        if(hand >= MAX_FRAMES) hand = 0;
        return victim_frame;
    }
};

class AGING : public Pager{
    public:
    AGING(){
        int hand = 0;
    }
    frame_t* select_victim_frame(){
    frame_t* victim_frame = NULL;
    int curr_hand_position = hand;
    do{
        //cout << "hand" << hand << endl;
        frame_t* frame = &frame_table[hand];
        pte_t* pte = &((pro_vector.at(frame->curr_pid))->page_table[frame->curr_ass_vpage]);
        frame->age_counter = frame->age_counter >> 1;
        if(pte->refrenced){
            frame->age_counter = frame->age_counter | 0x80000000;
            pte->refrenced = 0;  
        }
        //cout << "frame->age_counter" << frame->age_counter << endl;
        if(victim_frame == NULL || frame->age_counter < victim_frame->age_counter){
            victim_frame = frame;
        }
        
        hand = hand + 1;
        if(hand >= MAX_FRAMES)
        hand = 0;
    }
    while(hand != curr_hand_position);
    hand = victim_frame->fid + 1;
    if(hand >= MAX_FRAMES) hand = 0;
    return victim_frame;
    }

};

class WORKING_SET : public Pager{
    public:
    WORKING_SET(){
        int hand = 0;
    }
    frame_t* select_victim_frame(){
        frame_t* victim_frame = NULL;
        int curr_hand_pos = hand;
        do{
            frame_t* frame = &frame_table[hand];
            pte_t* pte = &((pro_vector.at(frame->curr_pid))->page_table[frame->curr_ass_vpage]);
            //cout << "hand" << hand << endl;
            //cout << "frame time" << endl;
            //cout << frame->time_of_last_use << endl;
            int diff = instr_count_num - frame->time_of_last_use;
            if(pte->refrenced){
                frame->time_of_last_use = instr_count_num;
                pte->refrenced = 0;
                if(victim_frame == NULL){
                    victim_frame = frame;
                }
            }
            else{
                if(diff > 49){
                    victim_frame = frame;
                    break;
                }
                else{
                    if(victim_frame == NULL || frame->time_of_last_use < victim_frame->time_of_last_use ){
                        victim_frame = frame;
                    }

                }

            }
            //cout << "FID"<<victim_frame->fid << endl;
            hand = hand + 1;
            if(hand>= MAX_FRAMES)
            hand = 0;
        }
        while(hand != curr_hand_pos);
        //cout << "((hand" << hand << endl;
        //cout << victim_frame->fid << endl;
        hand = victim_frame->fid + 1;
        if(hand >= MAX_FRAMES) hand = 0;
        return victim_frame;
    }


};

Pager* THE_PAGER;

void print_vma_list(Process* p){
    cout << "VMA LIST" << endl;
    vector<VMA*>::iterator itr = p->vma_list.begin();
    while( itr != p->vma_list.end()){
        cout << "start_vpage" << (*itr)->start_vpage;
        cout << "end_vpage" << (*itr)->end_vpage;
        cout << "write_protected" << (*itr)->write_protected;
        cout << "file_mapped" << (*itr)->file_mapped;
        itr++;
    }

}



void print_pro_vector(){
    vector<Process*>::iterator itr = pro_vector.begin();
    while( itr != pro_vector.end()){
        cout << "id" << (*itr)->pid;
        print_vma_list(*itr);
        itr++;
    }

}

int parseInput(int argc, char *argv[]) {
    int flag;
    int nofarg = 0;
    while ((flag = getopt(argc, argv, "o:a:f:")) != -1) {
        nofarg++;
        char temp;
        char algoType;
        if (flag == 'a') {
            // cout << optarg << endl;
            algoType = optarg[0];
            if (algoType == 'f') {
                THE_PAGER = new FIFO();
            } 
            else if (algoType == 'c') {
                THE_PAGER = new CLOCK();
            } else if (algoType == 'r') {
                THE_PAGER = new RANDOM();
            }
            else if (algoType == 'e') {
                THE_PAGER = new ESC();
            }
            else if (algoType == 'a') {
                THE_PAGER = new AGING();
            }
            else if (algoType == 'w') {
                 THE_PAGER = new WORKING_SET();
            }
        }
        else if(flag == 'f'){
            MAX_FRAMES = stoi(optarg);
            
        }
         else {
            cout << "Invalid Argument";
            exit(0);
        }
    }
    return nofarg;
    // read the input files
}

void readrfile(){
    int i = 0;
    fgets(rbuf,1024, rfile);
    char* tok = strtok(rbuf, DELIM);
    rlinenum = (atoi(tok));
    for(int i = 0; i < rlinenum;i++){
        fgets(rbuf,1024, rfile);
        char* tok = strtok(rbuf, DELIM);
        randvals.push_back(atoi(tok));
        i++;
    }
}




void readinpfile(){
    while(fgets(linebuf,1024, inpfile)){
    if(linebuf[0] == '#')
        continue;
    else
    break;
    }
    char* tok = strtok(linebuf, DELIM);
    int num_process = atoi(tok);
    int i = -1;
    while(num_process){  
        fgets(linebuf,1024, inpfile);
        if(linebuf[0] == '#')
        continue;
        i++;
        Process * p = new Process();
        p->pid = i;
        char* tok = strtok(linebuf, DELIM);
        int vma_num = atoi(tok);
        while(vma_num){
            VMA* v = new VMA();
            fgets(linebuf,1024, inpfile);
            if(linebuf[0] == '#')
            continue;
            char* sp = strtok(linebuf, DELIM);
            v->start_vpage = atoi(sp);
            char* ep = strtok(NULL, DELIM);
            v->end_vpage = atoi(ep);
            char* wp = strtok(NULL, DELIM);
            v->write_protected = atoi(wp);
            char* fm = strtok(NULL, DELIM);
            v->file_mapped = atoi(fm);
            p->vma_list.push_back(v);
            vma_num--;
        }
        num_process--;
        pro_vector.push_back(p);
    }
    //print_pro_vector();
}

void init_frame_table_and_list(){
    for(int i=0; i<MAX_FRAMES; i++){
        frame_table[i].fid = i;
        frame_t* f ;
        f = &frame_table[i];
        framefreelist.push(f);
    }

}

// bool check_file_mapped(Process* p, int vpage){
//     vector<VMA*>::iterator it = p->vma_list.begin();
//     while(it != p->vma_list.end()){
//         if(vpage>=(*it)->start_vpage && vpage<=(*it)->end_vpage){
//             if((*it)->file_mapped == 1)
//             return true;
//             else
//             return false;
//         }
//         it++;
//     }
//     return false;
// }

void unmap_victim_frame(frame_t* victim_frame){
    int pro_id = victim_frame->curr_pid;
    pte_t* pte = &((pro_vector.at(pro_id))->page_table[victim_frame->curr_ass_vpage]);
    pte->present = 0;
    //pte.refrenced = 0;
    //pte.modified = 0;
    (pro_vector.at(pro_id))->UNMAP = (pro_vector.at(pro_id))->UNMAP + 1;
    cost = cost + 400;
    cout << " UNMAP" << " " <<pro_id << ":" << victim_frame->curr_ass_vpage << endl;

    // if(){
    // }
}

frame_t* get_frame(){
    //cout << "in get frame" << endl;
    if(!framefreelist.empty()){
    frame_t* f = framefreelist.front();
    framefreelist.pop();
    return f;
    }
    else{
        //cout << "hey" << endl;
        frame_t* f = THE_PAGER->select_victim_frame();
        unmap_victim_frame(f);
        Process* p = pro_vector.at(f->curr_pid);
        pte_t* pte = &(p->page_table[f->curr_ass_vpage]);
        //cout << "M" << pte->modified;
            if(pte->modified && !pte->file_mapped){
                pte->modified = 0;
                pte->pagedout = 1;
                p->OUT = p->OUT + 1;
                cost = cost + 2700;
                cout << " OUT" << endl;
            }
            else if(pte->modified && pte->file_mapped){
                pte->modified = 0;
                p->FOUT = p->FOUT + 1;
                cost = cost + 2400;
                cout << " FOUT" << endl;
            }
        return f;
    }
    
}


bool get_instr(){
    if(fgets(linebuf,1024, inpfile) == NULL){
    //cout << "no";
    return false;
    }
    else {
        if(linebuf[0] == '#'){
            //cout << "hey";
            return get_instr();
        }
        else{
            char* co = strtok(linebuf, DELIM);
            curr_operation = co[0];
            char* vp = strtok(NULL, DELIM);
            vpage = atoi(vp);
            //instr_count_num++;
            return true;
        }
    }

}

VMA* check_in_vma_list(Process* p, int vpage){
    vector<VMA*>::iterator it = p->vma_list.begin();
    while(it != p->vma_list.end()){
        if(vpage>=(*it)->start_vpage && vpage<=(*it)->end_vpage){
            return (*it);
        }
        it++;
    }
    return NULL;
}

void after_exit(Process* p){
    for(int i = 0; i<MAX_VPAGES ; i++){
        pte_t* pte = &p->page_table[i];
        if(pte->present){
            int fid = p->page_table[i].frame_num;
            frame_t* f = &frame_table[fid];
            pte->present = 0;
            p->UNMAP = p->UNMAP + 1;
            cost = cost + 400;
            cout << " UNMAP" << " " <<p->pid << ":" << f->curr_ass_vpage << endl;
            f->age_counter = 0;
            f->curr_pid = -1;
            f->curr_ass_vpage = -1;
            frame_t* f_ptr = &frame_table[fid];
            framefreelist.push(f_ptr);
            if(pte->file_mapped){
                p->FOUT = p->FOUT + 1;
                cost = cost + 2400;
                cout << " FOUT" <<  endl;
            }
        }

    }
}



// bool check_write_protect(Process* p, int vpage){
//     vector<VMA*>::iterator it = p->vma_list.begin();
//     while(it != p->vma_list.end()){
//         if(vpage>=(*it)->start_vpage && vpage<=(*it)->end_vpage){
//             if((*it)->write_protected == 1)
//             return true;
//             else
//             return false;
//         }
//         it++;
//     }
//     return false;
// }

void simulation(){
    while(get_instr()){
        instr_count_num++;
        cout << instr_count_num << ": ==> " << curr_operation<< " " << vpage << endl;
        if (curr_operation == 'c'){
            CURRENT_PROCESS = pro_vector.at(vpage);
            Context_Switch = Context_Switch + 1;
            cost = cost + 130;
        }
        else if(curr_operation == 'e'){
            cout << "EXIT current process " << CURRENT_PROCESS  ->pid << '\n';
            after_exit(CURRENT_PROCESS);
            process_exits = process_exits + 1;
            cost = cost + 1250;
            // code to free all frames and everything for that process
        }
        else if((curr_operation == 'r')||(curr_operation == 'w')){
            cost = cost + 1;
            VMA* ass_vma = check_in_vma_list(CURRENT_PROCESS, vpage);
            if(ass_vma == NULL){
               CURRENT_PROCESS->SEGV = CURRENT_PROCESS->SEGV + 1;
               cost = cost + 340;
               cout << " SEGV" << endl;
            }
            else{
            pte_t *pte = &CURRENT_PROCESS->page_table[vpage];
            pte->file_mapped = ass_vma->file_mapped;
            pte->write_protected = ass_vma->write_protected;
            if(!pte->present){
                frame_t *newframe = get_frame();
                if(pte->file_mapped){
                    CURRENT_PROCESS->FIN = CURRENT_PROCESS->FIN + 1;
                    cost = cost + 2800;
                    cout<<" FIN"<<endl;
                }
                else if(!pte->pagedout){
                    CURRENT_PROCESS->ZERO = CURRENT_PROCESS->ZERO + 1;
                    cost = cost + 140;
                    cout << " ZERO" << endl;
                }
                else if(pte->pagedout){
                    CURRENT_PROCESS->IN = CURRENT_PROCESS->IN + 1;
                    cost = cost + 3100;
                    cout << " IN" << endl;
                }
                newframe->curr_pid = CURRENT_PROCESS->pid;
                newframe->curr_ass_vpage = vpage;
                newframe->age_counter = 0;
                pte->frame_num = newframe->fid;
                pte->present = 1;
                CURRENT_PROCESS->MAP = CURRENT_PROCESS->MAP + 1;
                cost = cost + 300;
                cout << " MAP" << " " <<pte->frame_num << endl; 
            }
            pte->refrenced = 1;
            if(curr_operation == 'w'){
                if(!pte->write_protected){
                     pte->modified = 1;
                }
                else{
                    CURRENT_PROCESS->SEGPROT = CURRENT_PROCESS->SEGPROT + 1;
                    cost = cost + 420;
                    cout << " SEGPROT" << endl;
                }
            }
                    
                }
            }
        // else if(curr_operation == 'w'){
        //      if(!check_in_vma_list(CURRENT_PROCESS, vpage)){
        //        cout << "SEGV" << endl;
        //     }
        //     else if(check_write_protect(CURRENT_PROCESS,vpage)){
        //        cout << "SEGPROT" << endl;
        //     }
        //     else{
        //     pte_t *pte = &CURRENT_PROCESS->page_table[vpage];
        //     if(!pte->present){
        //         frame_t *newframe = get_frame();
        //         newframe->curr_pid = CURRENT_PROCESS->pid;
        //         newframe->curr_ass_vpage = vpage;
        //         pte->frame_num = newframe->fid;
        //         pte->refrenced = 1;
        //         pte->modified = 1;
        //     }
        //     }

        // }
        else{
            cout << "invalid instruction" << endl;
        }

        }

}

void print_frame_table(){
    printf("FT:");
    for (int i = 0; i < MAX_FRAMES; i++)
    {
        printf(" ");
        if (frame_table[i].curr_pid != -1)
        {
            printf("%d:%d", frame_table[i].curr_pid, frame_table[i].curr_ass_vpage);
        }
        else
        {
            printf("*");
        }
    }
    printf("\n");


}

void print_process_table(){
     for (int i = 0; i < pro_vector.size(); i++)
    {
        // print table
        Process *proc = pro_vector.at(i);
        printf("PT[%d]:", proc->pid);

        for (int j = 0; j < 64; j++)
        {
            cout << " ";
            pte_t *pte = &proc->page_table[j];
            //cout << pte->present << endl;
            if (pte->present)
            {
                cout << j << ":";
                if (pte->refrenced)
                    cout << "R";
                else
                    cout << "-";
                if (pte->modified)
                    cout << "M";
                else
                    cout << "-";
                if (pte->pagedout)
                    cout << "S";
                else
                    cout << "-";
            }
            else
            {
                cout << (pte->pagedout ? "#" : "*");
            }
        }
        cout << endl;
    }
}

void print_process_stats(){
    for (int i = 0; i < pro_vector.size(); i++){
        printf("PROC[%d]: U=%lu M=%lu I=%lu O=%lu FI=%lu FO=%lu Z=%lu SV=%lu SP=%lu\n",
            pro_vector.at(i)->pid, pro_vector.at(i)->UNMAP, pro_vector.at(i)->MAP, 
            pro_vector.at(i)->IN, pro_vector.at(i)->OUT, pro_vector.at(i)->FIN,
            pro_vector.at(i)->FOUT, pro_vector.at(i)->ZERO, pro_vector.at(i)->SEGV, pro_vector.at(i)->SEGPROT);
    }

}


int main(int argc, char *argv[]){
   int nofarg = parseInput(argc, argv);
   inpfile = fopen(argv[nofarg+1], "r");
   rfile = fopen(argv[nofarg+2], "r");
   readinpfile();
   readrfile();
   //cout << "rlinenum"<<rlinenum << endl;
   init_frame_table_and_list();
   simulation();
   print_process_table();
   print_frame_table();
   print_process_stats();
   pte_t pte1;
   printf("TOTALCOST %lu %lu %lu %llu %lu\n",
           instr_count_num+1, Context_Switch, process_exits, cost, sizeof(pte1));
}